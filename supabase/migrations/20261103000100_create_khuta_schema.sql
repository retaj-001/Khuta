create extension if not exists pgcrypto;

create table public.devices (
  id uuid primary key default gen_random_uuid(),
  device_name text not null check (char_length(device_name) between 1 and 100),
  device_type text not null check (char_length(device_type) between 1 and 50),
  device_key text not null unique check (device_key ~ '^[a-z0-9][a-z0-9-]{2,63}$'),
  secret_hash text not null check (secret_hash ~ '^[0-9a-f]{64}$'),
  is_active boolean not null default true,
  created_at timestamptz not null default now()
);

create table public.sensor_readings (
  id bigint generated always as identity primary key,
  device_id uuid not null references public.devices(id) on delete restrict,
  reading_id text not null check (char_length(reading_id) between 8 and 80),
  temperature_c numeric(6,2) check (temperature_c between -55 and 125),
  acceleration_ms2 numeric(8,3) check (acceleration_ms2 between 0 and 200),
  speed_kmh numeric(7,2) check (speed_kmh between 0 and 200),
  latitude numeric(9,6) check (latitude between -90 and 90),
  longitude numeric(9,6) check (longitude between -180 and 180),
  satellites smallint check (satellites between 0 and 100),
  recorded_at timestamptz not null default now(),
  constraint sensor_readings_location_pair check ((latitude is null) = (longitude is null)),
  constraint sensor_readings_device_reading_unique unique (device_id, reading_id)
);

create index sensor_readings_device_recorded_idx
  on public.sensor_readings (device_id, recorded_at desc);

create table public.device_viewers (
  device_id uuid not null references public.devices(id) on delete cascade,
  user_id uuid not null references auth.users(id) on delete cascade,
  created_at timestamptz not null default now(),
  primary key (device_id, user_id)
);

alter table public.devices enable row level security;
alter table public.sensor_readings enable row level security;
alter table public.device_viewers enable row level security;

create policy "authorized viewers can read assigned devices"
  on public.devices for select to authenticated
  using (exists (
    select 1 from public.device_viewers v
    where v.device_id = devices.id and v.user_id = (select auth.uid())
  ));

create policy "authorized viewers can read assigned readings"
  on public.sensor_readings for select to authenticated
  using (exists (
    select 1 from public.device_viewers v
    where v.device_id = sensor_readings.device_id and v.user_id = (select auth.uid())
  ));

create policy "viewers can inspect their own assignments"
  on public.device_viewers for select to authenticated
  using (user_id = (select auth.uid()));

revoke all on public.devices from anon, authenticated;
revoke all on public.sensor_readings from anon, authenticated;
revoke all on public.device_viewers from anon, authenticated;
grant select on public.devices, public.sensor_readings, public.device_viewers to authenticated;
grant select, insert, update on public.devices, public.sensor_readings to service_role;
grant usage, select on sequence public.sensor_readings_id_seq to service_role;

create or replace function public.latest_public_reading(p_device_key text)
returns table (
  temperature_c numeric,
  acceleration_ms2 numeric,
  speed_kmh numeric,
  latitude numeric,
  longitude numeric,
  satellites smallint,
  recorded_at timestamptz
)
language sql stable security definer
set search_path = public
as $$
  select r.temperature_c, r.acceleration_ms2, r.speed_kmh,
         null::numeric as latitude, null::numeric as longitude,
         r.satellites, r.recorded_at
  from public.sensor_readings r
  join public.devices d on d.id = r.device_id
  where d.device_key = p_device_key and d.is_active
  order by r.recorded_at desc
  limit 1;
$$;

create or replace function public.latest_authorized_reading(p_device_key text)
returns table (
  temperature_c numeric,
  acceleration_ms2 numeric,
  speed_kmh numeric,
  latitude numeric,
  longitude numeric,
  satellites smallint,
  recorded_at timestamptz
)
language sql stable security definer
set search_path = public
as $$
  select r.temperature_c, r.acceleration_ms2, r.speed_kmh,
         r.latitude, r.longitude, r.satellites, r.recorded_at
  from public.sensor_readings r
  join public.devices d on d.id = r.device_id
  join public.device_viewers v on v.device_id = d.id
  where d.device_key = p_device_key and d.is_active and v.user_id = auth.uid()
  order by r.recorded_at desc
  limit 1;
$$;

revoke all on function public.latest_public_reading(text) from public;
revoke all on function public.latest_authorized_reading(text) from public;
grant execute on function public.latest_public_reading(text) to anon, authenticated;
grant execute on function public.latest_authorized_reading(text) to authenticated;

comment on function public.latest_public_reading(text) is
  'Public dashboard projection. Precise coordinates are deliberately redacted.';
comment on function public.latest_authorized_reading(text) is
  'Returns precise coordinates only to a user assigned in device_viewers.';
