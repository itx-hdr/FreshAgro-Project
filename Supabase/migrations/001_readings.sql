create table if not exists public.readings (
  device_id text not null,
  sample_id text not null,
  sampled_at timestamptz,
  received_at timestamptz not null default now(),
  o2_valid boolean not null,
  co2_valid boolean not null,
  sht_valid boolean not null,
  oxygen numeric,
  co2 integer,
  temperature numeric,
  humidity numeric,
  firmware text not null,
  primary key (device_id, sample_id),
  check ((o2_valid and oxygen between 0 and 25) or (not o2_valid and oxygen is null)),
  check ((co2_valid and co2 between 0 and 1000000) or (not co2_valid and co2 is null)),
  check ((sht_valid and temperature between -45 and 130 and humidity between 0 and 100) or
         (not sht_valid and temperature is null and humidity is null)),
  check (not o2_valid or oxygen is not null),
  check (not co2_valid or co2 is not null),
  check (not sht_valid or (temperature is not null and humidity is not null))
);
create index if not exists readings_received_idx on public.readings (device_id, received_at desc);
alter table public.readings enable row level security;
revoke all on public.readings from anon, authenticated;
grant select on public.readings to anon, authenticated;
create policy "Public telemetry read only" on public.readings for select to anon, authenticated using (true);
-- No public insert/update/delete policy. Only the authenticated ingestion function writes.
