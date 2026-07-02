const MSV2_CACHE = "msv2-cloud-app-v1";
const MSV2_ASSETS = [
  "/",
  "/index.html",
  "/manifest.webmanifest",
  "/app-icon.svg",
  "/assets/plant_gifs/cactus_cluster.gif",
  "/assets/plant_gifs/cup_plant.gif",
  "/assets/plant_gifs/monstera.gif",
  "/assets/plant_gifs/spindly.gif",
  "/assets/plant_gifs/sprout_pot.gif",
  "/assets/plant_gifs/wide_planter.gif"
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
  event.respondWith(
    caches.match(event.request).then((cached) =>
      cached || fetch(event.request).then((response) => {
        if (new URL(event.request.url).origin === self.location.origin) {
          const copy = response.clone();
          caches.open(MSV2_CACHE).then((cache) => cache.put(event.request, copy));
        }
        return response;
      })
    )
  );
});
