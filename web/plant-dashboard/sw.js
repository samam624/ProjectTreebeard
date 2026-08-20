const MSV2_CACHE = "msv2-cloud-app-v13";
const MSV2_ASSETS = [
  "/",
  "/index.html",
  "/manifest.webmanifest",
  "/app-icon.svg",
  "/assets/app.v13.js",
  "/assets/jsQR-1.4.0.min.js",
  "/assets/plant_gifs/cactus_cluster.gif",
  "/assets/plant_gifs/cup_plant.gif",
  "/assets/plant_gifs/monstera.gif",
  "/assets/plant_gifs/spindly.gif",
  "/assets/plant_gifs/sprout_pot.gif",
  "/assets/plant_gifs/wide_planter.gif",
  "/assets/plant_gifs/color_bonsai_bowl.gif",
  "/assets/plant_gifs/color_grass_planter.gif",
  "/assets/plant_gifs/color_hanging_clump.gif",
  "/assets/plant_gifs/color_hanging_vine.gif",
  "/assets/plant_gifs/color_round_pot.gif",
  "/assets/plant_gifs/color_vine_pot.gif",
  "/assets/plant_gifs/color_zigzag_pot.gif"
];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(MSV2_CACHE).then((cache) => cache.addAll(MSV2_ASSETS)));
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((key) => key !== MSV2_CACHE).map((key) => caches.delete(key)))
    )
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  const url = new URL(event.request.url);
  const isAppShell =
    event.request.mode === "navigate" ||
    url.pathname === "/" ||
    url.pathname.endsWith("/index.html");
  if (isAppShell) {
    event.respondWith(
      fetch(event.request, { cache: "no-store" }).then((response) => {
        const copy = response.clone();
        caches.open(MSV2_CACHE).then((cache) => cache.put(event.request, copy));
        return response;
      }).catch(() => caches.match(event.request))
    );
    return;
  }
  // Only same-origin /assets/* (versioned filenames, immutable per Netlify's own
  // _headers policy) are safe to cache-first. Everything else - especially the
  // cross-origin calls to the Supabase API that carry live moisture/battery/snapshot
  // data - must always hit the network. This used to fall into the cache-first branch
  // below like any other GET, which meant a normal reload kept serving whatever snapshot
  // response got cached on first load, forever - only a hard reload (which bypasses the
  // service worker entirely, so every fetch including the data call goes straight to
  // network) ever actually saw fresh data. Fixed 2026-08-17.
  const isCacheableAsset = url.origin === self.location.origin && url.pathname.startsWith("/assets/");
  if (!isCacheableAsset) {
    event.respondWith(fetch(event.request));
    return;
  }

  event.respondWith(
    caches.match(event.request).then((cached) =>
      cached || fetch(event.request).then((response) => {
        const copy = response.clone();
        caches.open(MSV2_CACHE).then((cache) => cache.put(event.request, copy));
        return response;
      })
    )
  );
});
