function validateBatch(body, deviceId, now = Date.now()) {
  if (!body || !Array.isArray(body.samples) || body.samples.length < 1 || body.samples.length > 72) throw Error('Expected 1–72 samples');
  const seen = new Set();
  return body.samples.map(s => {
    if (!s || !/^[a-f0-9]{16}$/.test(s.sample_id) || seen.has(s.sample_id)) throw Error('Invalid or duplicate sample ID');
    seen.add(s.sample_id);
    for (const key of ['o2_valid', 'co2_valid', 'sht_valid']) if (typeof s[key] !== 'boolean') throw Error('Invalid sensor status');
    const number = (key, valid, min, max, integer = false) => {
      const v = s[key];
      if (!valid) { if (v !== null) throw Error('Invalid readings must be null'); return null; }
      if (typeof v !== 'number' || !Number.isFinite(v) || v < min || v > max || (integer && !Number.isInteger(v))) throw Error('Invalid ' + key);
      return v;
    };
    if (s.sampled_at !== null && (!Number.isInteger(s.sampled_at) || s.sampled_at < 1704067200 || s.sampled_at * 1000 > now + 300000)) throw Error('Invalid sample time');
    if (typeof s.firmware !== 'string' || !/^[\w.-]{1,32}$/.test(s.firmware)) throw Error('Invalid firmware version');
    return {device_id: deviceId, sample_id: s.sample_id, sampled_at: s.sampled_at === null ? null : new Date(s.sampled_at * 1000).toISOString(),
      o2_valid: s.o2_valid, co2_valid: s.co2_valid, sht_valid: s.sht_valid,
      oxygen: number('oxygen', s.o2_valid, 0, 25), co2: number('co2', s.co2_valid, 0, 1000000, true),
      temperature: number('temperature', s.sht_valid, -45, 130), humidity: number('humidity', s.sht_valid, 0, 100), firmware: s.firmware};
  });
}

const json = (body: unknown, status = 200) => new Response(JSON.stringify(body), {status, headers: {'Content-Type': 'application/json'}});
Deno.serve(async (request: Request) => {
  if (request.method !== 'POST') return json({error: 'POST required'}, 405);
  const expected = Deno.env.get('DEVICE_TOKEN_SHA256');
  const deviceId = Deno.env.get('DEVICE_ID');
  if (!expected || !/^[a-f0-9]{64}$/.test(expected) || !deviceId) return json({error: 'Server configuration missing'}, 503);
  const token = request.headers.get('x-device-token') || '';
  if (token.length < 32 || token.length > 128) return json({error: 'Unauthorized'}, 401);
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(token))), b => b.toString(16).padStart(2, '0')).join('');
  let mismatch = 0;
  for (let i = 0; i < 64; i++) mismatch |= digest.charCodeAt(i) ^ expected.charCodeAt(i);
  if (mismatch) return json({error: 'Unauthorized'}, 401);
  // Bound streaming body allocation even when Content-Length is absent.
  const reader = request.body?.getReader();
  if (!reader) return json({error: 'Missing body'}, 400);
  let text = '', size = 0;
  const decoder = new TextDecoder();
  while (true) {
    const {value, done} = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > 40000) { await reader.cancel(); return json({error: 'Payload too large'}, 413); }
    text += decoder.decode(value, {stream: true});
  }
  text += decoder.decode();
  let rows;
  try { rows = validateBatch(JSON.parse(text), deviceId); }
  catch { return json({error: 'Invalid telemetry payload'}, 400); }
  try {
    const result = await fetch(`${Deno.env.get('SUPABASE_URL')}/rest/v1/readings?on_conflict=device_id,sample_id`, {
      method: 'POST', signal: AbortSignal.timeout(8000),
      headers: {'apikey': Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!, 'Authorization': `Bearer ${Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')}`, 'Content-Type': 'application/json', 'Prefer': 'resolution=ignore-duplicates,return=minimal'},
      body: JSON.stringify(rows)
    });
    if (!result.ok) return json({error: 'Storage unavailable'}, 503);
    // Atomic batch success; duplicate IDs are already durably stored.
    return json({ok: true, accepted: rows.length});
  } catch { return json({error: 'Storage unavailable'}, 503); }
});
