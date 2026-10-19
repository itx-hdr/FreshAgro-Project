export function validateBatch(body, deviceId, now = Date.now()) {
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
