# Furever Home — single-service container.
#
# The relay (web/relay.js) serves BOTH the static web app and the cross-device
# hand-off endpoints. It reads PORT from the env and binds 0.0.0.0, so it drops
# straight onto Fly/Render/Cloud Run. There is no build step: the FHE engine is
# prebuilt WebAssembly committed in web/wasm/ (all crypto runs in the browser),
# and relay.js has zero npm dependencies.
#
# This same Dockerfile is the template for any "relay-style" FHE demo app
# (archetype C): copy it, keep your web/ + relay, done.
FROM node:20-alpine
WORKDIR /app
COPY . .
ENV PORT=8080
EXPOSE 8080
CMD ["node", "web/relay.js"]
