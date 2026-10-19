import { validateBatch } from './validation.mjs';
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
