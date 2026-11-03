import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const jsonHeaders = { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" };
const fail = (status: number, error: string) => new Response(JSON.stringify({ error }), { status, headers: jsonHeaders });
const numberOrNull = (value: unknown, min: number, max: number) => {
  if (value === null || value === undefined) return null;
  if (typeof value !== "number" || !Number.isFinite(value) || value < min || value > max) throw new Error("invalid-number");
  return value;
};
const sha256 = async (value: string) => {
  const bytes = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return [...new Uint8Array(bytes)].map((item) => item.toString(16).padStart(2, "0")).join("");
};
const safeEqual = (left: string, right: string) => {
  if (left.length !== right.length) return false;
  let difference = 0;
  for (let index = 0; index < left.length; index += 1) difference |= left.charCodeAt(index) ^ right.charCodeAt(index);
  return difference === 0;
};

Deno.serve(async (request) => {
  if (request.method !== "POST") return fail(405, "method-not-allowed");
  const contentLength = Number(request.headers.get("content-length") ?? "0");
  if (contentLength > 4096) return fail(413, "payload-too-large");

  const deviceKey = request.headers.get("x-device-id") ?? "";
  const deviceSecret = request.headers.get("x-device-key") ?? "";
  if (!/^[a-z0-9][a-z0-9-]{2,63}$/.test(deviceKey) || deviceSecret.length < 32 || deviceSecret.length > 256) {
    return fail(401, "invalid-device-credentials");
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL");
  const serviceRoleKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  if (!supabaseUrl || !serviceRoleKey) return fail(500, "server-not-configured");
  const supabase = createClient(supabaseUrl, serviceRoleKey, { auth: { persistSession: false } });

  const { data: device, error: deviceError } = await supabase
    .from("devices").select("id, secret_hash, is_active").eq("device_key", deviceKey).maybeSingle();
  const presentedHash = await sha256(deviceSecret);
  if (deviceError || !device || !device.is_active || !safeEqual(device.secret_hash, presentedHash)) {
    return fail(401, "invalid-device-credentials");
  }

  let input: Record<string, unknown>;
  try { input = await request.json(); } catch { return fail(400, "invalid-json"); }
  const readingId = typeof input.reading_id === "string" ? input.reading_id : "";
  if (!/^[a-zA-Z0-9-]{8,80}$/.test(readingId)) return fail(422, "invalid-reading-id");

  const { data: existing } = await supabase.from("sensor_readings").select("id")
    .eq("device_id", device.id).eq("reading_id", readingId).maybeSingle();
  if (existing) return new Response(JSON.stringify({ ok: true, duplicate: true }), { status: 200, headers: jsonHeaders });

  const { data: latest } = await supabase.from("sensor_readings").select("recorded_at")
    .eq("device_id", device.id).order("recorded_at", { ascending: false }).limit(1).maybeSingle();
  if (latest && Date.now() - Date.parse(latest.recorded_at) < 1000) return fail(429, "rate-limited");

  let reading;
  try {
    const latitude = numberOrNull(input.latitude, -90, 90);
    const longitude = numberOrNull(input.longitude, -180, 180);
    if ((latitude === null) !== (longitude === null)) return fail(422, "location-pair-required");
    const satellites = numberOrNull(input.satellites, 0, 100);
    if (satellites !== null && !Number.isInteger(satellites)) return fail(422, "invalid-satellites");
    reading = {
      device_id: device.id, reading_id: readingId,
      temperature_c: numberOrNull(input.temperature_c, -55, 125),
      acceleration_ms2: numberOrNull(input.acceleration_ms2, 0, 200),
      speed_kmh: numberOrNull(input.speed_kmh, 0, 200),
      latitude, longitude,
      satellites,
    };
  } catch { return fail(422, "invalid-reading"); }

  const { data: inserted, error: insertError } = await supabase.from("sensor_readings")
    .insert(reading).select("id, recorded_at").single();
  if (insertError?.code === "23505") return new Response(JSON.stringify({ ok: true, duplicate: true }), { status: 200, headers: jsonHeaders });
  if (insertError) return fail(500, "insert-failed");
  return new Response(JSON.stringify({ ok: true, id: inserted.id, recorded_at: inserted.recorded_at }), { status: 201, headers: jsonHeaders });
});
