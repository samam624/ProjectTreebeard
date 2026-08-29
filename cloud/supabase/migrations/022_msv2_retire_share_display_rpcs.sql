-- The Netlify dashboard's brightness/screen-on control (web/plant-dashboard) has been
-- removed: the hosted app is meant to stay strictly read-only (see
-- docs/PRODUCT_ROADMAP.md and docs/STATUS_HANDOFF.md), and the local (LAN-served) app
-- already owns every writable setting, including display brightness via its own /ui
-- endpoint. The two RPCs below were only ever called by that Netlify control's edge
-- function route (plant-dashboard's now-removed shareDisplayApi/"/api/share-display"),
-- so retire them rather than leaving a live, publicly-reachable (given a valid share
-- token) write/read surface with nothing legitimate left calling it.
--
-- Deliberately NOT touched: msv2_upsert_receiver_display_settings and
-- msv2_read_receiver_pending_display_settings (no "share_" prefix). Those back the
-- physical display's own touchscreen-brightness cloud sync (receiver-snapshot's
-- POST/GET ?display=1), which is unrelated to the Netlify app and must keep working.

drop function if exists public.msv2_upsert_share_receiver_display_settings(text, text, integer, boolean);
drop function if exists public.msv2_read_share_receiver_display_settings(text);
