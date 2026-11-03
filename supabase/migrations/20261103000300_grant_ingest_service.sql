-- The project was created with automatic Data API grants disabled. Grant only
-- the trusted Edge Function service role the permissions its ingest path needs.
grant select, insert, update on public.devices, public.sensor_readings to service_role;
grant usage, select on sequence public.sensor_readings_id_seq to service_role;
