# Cloud: database and dashboard

The backend every receiver uploads to, and the phone dashboard that reads from it. Supabase holds
the history, calibration and receiver accounts behind row-level security; Netlify hosts the
dashboard.

| | |
| --- | --- |
| **Database** | [`supabase/migrations/`](supabase/migrations/): every schema change, in order |
| **Edge functions** | [`supabase/functions/`](supabase/functions/): receiver uploads and the "water now" region (`receiver-snapshot`), receiver sign-up and claiming, the dashboard API |
| **Dashboard** | [`web/plant-dashboard/`](web/plant-dashboard/), live at [msv2-plants.netlify.app](https://msv2-plants.netlify.app) |
| **Tools** | [`scripts/`](scripts/): history export, replays of the moisture calibration, and checks |
| **Write-ups** | [`docs/`](docs/) |

Start with:
[Supabase setup](docs/SUPABASE_SETUP.md) ·
[how the moisture percent is made](docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md)
