-- Removes the only code path that could delete sensor history in bulk (2026-11-21).
--
-- msv2_clear_share_sensor_history (016, locked to service_role in 020) deleted EVERY history row
-- for a receiver. It was reachable from the receiver's local app (History tab, "Clear All
-- History", one browser confirm()) through plant-dashboard /api/share-history-clear. It was
-- added when pre-oscillator data was meaningless; the history is now months of real
-- measurements that must not be one tap from deletion. The button and the edge route were
-- removed in the same change; dropping the function means an old receiver firmware or a stale
-- deployment of the edge function can no longer delete anything either.
--
-- If history ever genuinely needs removing, do it by hand in SQL after taking an export.

drop function if exists public.msv2_clear_share_sensor_history(text, text);
