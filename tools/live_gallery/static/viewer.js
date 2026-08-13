import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

/*
 * Editable GLTF import layer
 * --------------------------
 * Keep corpus-specific behaviour here instead of baking it into the gallery.
 * GLTFLoader still handles normal glTF/GLB and its standard KHR/EXT extensions;
 * this registry is the deliberately small escape hatch for things that are
 * not part of the standard format.
 *
 * A custom extension can be added as:
 *
 *   CUSTOM_GLTF_EXTENSIONS.push((gltf, scene) => { ... });
 *
 * The callback receives the fully loaded Three.js scene, so it can annotate,
 * replace, or otherwise modify imported objects before they are displayed.
 */
const CUSTOM_GLTF_EXTENSIONS = [];

/*
 * Billboard support.
 *
 * Accepted metadata on a glTF node:
 *   extras: { "billboard": true }
 *   extras: { "billboard": "spherical" }
 *   extras: { "billboard": "cylindrical" }
 *   extras: { "billboard": { "mode": "cylindrical", "axis": "y" } }
 *
 * "spherical" faces the camera in all axes.
 * "cylindrical" rotates around the requested axis (default Y), keeping the
 * object's local up direction stable.
 *
 * This is intentionally an importer-side convention rather than pretending
 * billboard is a standard glTF feature. If the eventual canonical format
 * chooses another spelling, this is the one place that needs changing.
 */
const billboards = [];

function installBillboardSupport(gltf) {
  gltf.scene.traverse((object) => {
    const billboard = object.userData?.billboard;
    if (!billboard) return;

    let mode = 'spherical';
    let axis = 'y';
    if (typeof billboard === 'string') mode = billboard.toLowerCase();
    if (typeof billboard === 'object') {
      mode = String(billboard.mode || 'spherical').toLowerCase();
      axis = String(billboard.axis || 'y').toLowerCase();
    }

    billboards.push({ object, mode, axis });
  });
}

CUSTOM_GLTF_EXTENSIONS.push((gltf) => installBillboardSupport(gltf));

const container = document.getElementById('viewer');
const title = document.getElementById('title');
const status = document.getElementById('status');
const errorBox = document.getElementById('error');

// window.REL is set by a small inline bootstrap <script> in viewer.html,
// before this module loads -- the one piece of this page that's genuinely
// per-request (which file to view), so it can't live in this static file.
const REL = window.REL;

// Strip everything after the first '.' in the last path segment
const parts = REL.split('/');
const filename = parts.pop();
const base = filename.split('.')[0];
const MODEL_REL = [...parts, base].join('/') + '.glb';

const MODEL_URL = '/model/' + MODEL_REL.split('/').map(encodeURIComponent).join('/');
title.textContent = REL;
console.log(`loading ${MODEL_URL}…`);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x080a0e);

const camera = new THREE.PerspectiveCamera(45, 1, 0.01, 100000);
camera.position.set(2, 2, 3);

const renderer = new THREE.WebGLRenderer({ antialias:true, logarithmicDepthBuffer:true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1;
container.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.target.set(0, 0, 0);

scene.add(new THREE.HemisphereLight(0xffffff, 0x303848, 2.0));
const key = new THREE.DirectionalLight(0xffffff, 2.5);
key.position.set(4, 8, 5);
scene.add(key);
const fill = new THREE.DirectionalLight(0x9ab8ff, 1.0);
fill.position.set(-4, 2, -3);
scene.add(fill);

function frameObject(object) {
  const box = new THREE.Box3().setFromObject(object);
  if (box.isEmpty()) {
    camera.position.set(2, 2, 3);
    controls.target.set(0, 0, 0);
    return;
  }
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const radius = Math.max(size.length() * 0.5, 0.01);
  const distance = radius / Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5));
  camera.position.copy(center).add(new THREE.Vector3(1, .75, 1).normalize().multiplyScalar(distance * 1.35));
  camera.near = Math.max(radius / 1000, 0.001);
  camera.far = Math.max(radius * 1000, 100);
  camera.updateProjectionMatrix();
  controls.target.copy(center);
  controls.update();
}

function billboardRotation(entry) {
  const { object, mode, axis } = entry;
  const worldPos = new THREE.Vector3();
  const cameraPos = new THREE.Vector3();
  object.getWorldPosition(worldPos);
  camera.getWorldPosition(cameraPos);

  const direction = cameraPos.sub(worldPos);
  if (direction.lengthSq() < 1e-12) return;

  if (mode === 'cylindrical' || mode === 'cylinder') {
    const up = axis === 'z' ? new THREE.Vector3(0, 0, 1)
                            : axis === 'x' ? new THREE.Vector3(1, 0, 0)
                                           : new THREE.Vector3(0, 1, 0);
    direction.projectOnPlane(up).normalize();
    if (direction.lengthSq() < 1e-12) return;
    const target = worldPos.clone().add(direction);
    object.lookAt(target);
    return;
  }

  // Spherical: look directly at the camera.
  object.lookAt(cameraPos);
}

function applyEditableExtensions(gltf) {
  billboards.length = 0;
  for (const extension of CUSTOM_GLTF_EXTENSIONS) extension(gltf, scene);
}

async function loadEditableGLTF(url) {
  const loader = new GLTFLoader();

  // Future custom extensions can be registered with loader.register(parser => ...)
  // when they need access to raw glTF JSON/buffers. The post-load registry above
  // is intentionally simpler for scene-level features such as billboards.
  return await loader.loadAsync(url);
}

async function main() {
  try {
    const gltf = await loadEditableGLTF(MODEL_URL);
    applyEditableExtensions(gltf);
    scene.add(gltf.scene);
    frameObject(gltf.scene);

    const animations = gltf.animations?.length || 0;
    status.innerHTML = `<span style="color:var(--good)">loaded</span> · ` +
                       `${animations} animation${animations === 1 ? '' : 's'} · ` +
                       `${billboards.length} billboard${billboards.length === 1 ? '' : 's'}`;
  } catch (err) {
    console.error(err);
    status.innerHTML = '';
    errorBox.textContent = `Failed to load GLB: ${err?.message || err}`;
    errorBox.classList.add('show');
  }
}

function resize() {
  camera.aspect = window.innerWidth / Math.max(window.innerHeight, 1);
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
}
window.addEventListener('resize', resize);

function animate() {
  requestAnimationFrame(animate);
  for (const billboard of billboards) billboardRotation(billboard);
  controls.update();
  renderer.render(scene, camera);
}

resize();
main();
animate();
