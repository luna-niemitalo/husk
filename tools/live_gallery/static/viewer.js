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

/*
 * Additive-blend material rebuild (blend_mode 3/4 extras, WoW's
 * NoAlphaAdd/Add) -- the JS-side counterpart to
 * tools/corpus_scan_tasks/render_glb.py's fix_additive_materials().
 *
 * Core glTF has no additive-blend mode (gltf_mesh.hpp's own Material::
 * blendMode doc comment), so husk exports GLTFLoader's standard alpha-blend
 * shape (alphaMode BLEND) and leaves the real blend_mode as material
 * extras for a consumer to reinterpret. Left alone, GLTFLoader's default
 * MeshStandardMaterial (lit, THREE.NormalBlending) makes a glow/smoke
 * layer's mostly-black texture read as a solid dark, clearly-bordered
 * panel -- confirmed directly against a real corpus file (a raid helm's
 * additive smoke-plume sub-mesh rendering as a hard visible slab instead
 * of a soft wisp). WoW's own compositing contributes nothing where the
 * texture is black; the fix here is the same one render_glb.py already
 * uses -- treat the base-color texture as unlit emissive, composited via
 * real additive blending, so black contributes nothing and depth never
 * occludes what's behind it.
 */
function applyAdditiveBlending(gltf) {
  gltf.scene.traverse((object) => {
    const mats = Array.isArray(object.material) ? object.material : (object.material ? [object.material] : []);
    for (const mat of mats) {
      const blendMode = mat?.userData?.blend_mode;
      if (blendMode !== 3 && blendMode !== 4) continue;
      if (mat.userData.__additiveApplied) continue;
      mat.userData.__additiveApplied = true;

      mat.emissiveMap = mat.map;
      mat.emissive.setRGB(1, 1, 1);
      mat.emissiveIntensity = 1;
      mat.map = null;
      mat.color.setRGB(0, 0, 0);
      mat.metalness = 0;
      mat.roughness = 1;
      mat.blending = THREE.AdditiveBlending;
      mat.transparent = true;
      mat.depthWrite = false;
      mat.needsUpdate = true;
    }
  });
}

CUSTOM_GLTF_EXTENSIONS.push((gltf) => applyAdditiveBlending(gltf));

/*
 * Material curve animation (texture_transform_animation/tint_animation/
 * fade_animation extras) -- husk's own JS-side port of
 * tools/husk_blender_geoset_mask.py's apply_texture_transform_animation/
 * apply_tint_fade_animation, closing the "no JS-side port at all" gap
 * TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md flagged. Core glTF has no
 * animation-channel target for a material property (see gltf_mesh.hpp's
 * own doc comments on these extras) -- these are read and evaluated by
 * hand every frame instead, the same "diagnostic extras become real
 * playback in a companion script" treatment the Blender side already gets.
 *
 * Three.js's GLTFLoader merges each glTF material's own `extras` object
 * directly into `material.userData` (confirmed against GLTFLoader's own
 * `assignExtrasToUserData`, called from `loadMaterial()`) -- already plain
 * JS objects/arrays, no IDPropertyGroup-style unwrapping needed the way
 * Blender's own importer requires (see husk_blender_geoset_mask.py's
 * `_to_pyobj` doc comment for why that one's needed there and not here).
 *
 * Picks curves[0] (the first curve entry, regardless of `sequence_index`)
 * the same way the Blender-side implementation does -- a real, shared
 * simplification, not an oversight: see apply_texture_transform_animation's
 * own doc comment for why "whichever the importer already activates" is
 * the accepted answer here, not a guess re-derived from scratch.
 */
function lerp(a, b, t) { return a + (b - a) * t; }

function evalScalarCurve(keyframes, t) {
  if (!keyframes || !keyframes.length) return 0;
  if (t <= keyframes[0].time) return keyframes[0].value;
  for (let i = 0; i < keyframes.length - 1; i++) {
    const t0 = keyframes[i].time, v0 = keyframes[i].value;
    const t1 = keyframes[i + 1].time, v1 = keyframes[i + 1].value;
    if (t <= t1) return lerp(v0, v1, t1 === t0 ? 0 : (t - t0) / (t1 - t0));
  }
  return keyframes[keyframes.length - 1].value;
}

function evalVec3Curve(keyframes, t) {
  if (!keyframes || !keyframes.length) return [0, 0, 0];
  if (t <= keyframes[0].time) return keyframes[0].value;
  for (let i = 0; i < keyframes.length - 1; i++) {
    const t0 = keyframes[i].time, v0 = keyframes[i].value;
    const t1 = keyframes[i + 1].time, v1 = keyframes[i + 1].value;
    if (t <= t1) {
      const frac = t1 === t0 ? 0 : (t - t0) / (t1 - t0);
      return [lerp(v0[0], v1[0], frac), lerp(v0[1], v1[1], frac), lerp(v0[2], v1[2], frac)];
    }
  }
  return keyframes[keyframes.length - 1].value;
}

// Slerps the raw quaternion curve, then collapses to a single Z-axis angle
// (`2*atan2(qz, qw)`) -- same collapse gltf_mesh.cpp's textureTransformToKhr
// does for the constant case, and the same real limitation applies: a
// genuinely 3-axis-animated rotation has no honest representation as a
// single UV rotation either way.
function evalQuatCurveZAngle(keyframes, t) {
  if (!keyframes || !keyframes.length) return 0;
  const toQuat = (v) => new THREE.Quaternion(v[0], v[1], v[2], v[3]);
  let q = toQuat(keyframes[0].value);
  if (t > keyframes[0].time) {
    let found = false;
    for (let i = 0; i < keyframes.length - 1; i++) {
      const t0 = keyframes[i].time, v0 = keyframes[i].value;
      const t1 = keyframes[i + 1].time, v1 = keyframes[i + 1].value;
      if (t <= t1) {
        q = toQuat(v0).slerp(toQuat(v1), t1 === t0 ? 0 : (t - t0) / (t1 - t0));
        found = true;
        break;
      }
    }
    if (!found) q = toQuat(keyframes[keyframes.length - 1].value);
  }
  return 2 * Math.atan2(q.z, q.w);
}

function curveDuration(curves) {
  let m = 0;
  for (const c of curves || []) {
    const kfs = c.keyframes || [];
    if (kfs.length) m = Math.max(m, kfs[kfs.length - 1].time);
  }
  return m;
}

const materialCurveAnimations = [];

function collectMaterialCurveAnimations(gltf) {
  materialCurveAnimations.length = 0;
  const seen = new Set();
  gltf.scene.traverse((object) => {
    const mats = Array.isArray(object.material) ? object.material : (object.material ? [object.material] : []);
    for (const mat of mats) {
      if (!mat || seen.has(mat.uuid)) continue;
      seen.add(mat.uuid);
      const xf = mat.userData?.texture_transform_animation;
      const tint = mat.userData?.tint_animation;
      const fade = mat.userData?.fade_animation;
      if (!xf && !tint && !fade) continue;

      const translation = xf?.translation || null;
      const rotation = xf?.rotation || null;
      const scaling = xf?.scaling || null;
      const alphaCurves = fade?.alpha || null;
      const weightCurves = fade?.weight || null;
      const duration = Math.max(
        curveDuration(translation), curveDuration(rotation), curveDuration(scaling),
        curveDuration(tint), curveDuration(alphaCurves), curveDuration(weightCurves));
      if (duration <= 0) continue;

      // texture.offset/rotation/repeat need matrixAutoUpdate (default true)
      // to actually take effect on the next render -- not overridden here,
      // just relying on the default rather than assuming.
      materialCurveAnimations.push({ material: mat, translation, rotation, scaling, tint, alphaCurves, weightCurves, duration });
    }
  });
}

function updateMaterialCurveAnimations(elapsed) {
  for (const entry of materialCurveAnimations) {
    const t = entry.duration > 0 ? elapsed % entry.duration : 0;
    // applyAdditiveBlending (above) relocates a blend_mode 3/4 material's
    // texture from .map to .emissiveMap -- fall back to that so a curve
    // still finds its texture on such a material instead of silently
    // no-oping once .map goes null.
    const map = entry.material.map || entry.material.emissiveMap;
    if (entry.translation && map) {
      const [x, y] = evalVec3Curve(entry.translation[0].keyframes, t);
      // glTF's own on-disk UV convention already grows V downward, same as
      // WoW's -- unlike the Blender port (V grows upward there), no sign
      // flip is needed here. See this function's own module comment.
      map.offset.set(x, y);
    }
    if (entry.rotation && map) {
      map.rotation = evalQuatCurveZAngle(entry.rotation[0].keyframes, t);
    }
    if (entry.scaling && map) {
      const [x, y] = evalVec3Curve(entry.scaling[0].keyframes, t);
      map.repeat.set(x, y);
    }
    if (entry.tint) {
      const [r, g, b] = evalVec3Curve(entry.tint[0].keyframes, t);
      entry.material.color.setRGB(r, g, b);
    }
    if (entry.alphaCurves || entry.weightCurves) {
      const alpha = entry.alphaCurves ? evalScalarCurve(entry.alphaCurves[0].keyframes, t) : 1.0;
      const weight = entry.weightCurves ? evalScalarCurve(entry.weightCurves[0].keyframes, t) : 1.0;
      entry.material.opacity = alpha * weight;
    }
  }
}

const container = document.getElementById('viewer');
const title = document.getElementById('title');
const status = document.getElementById('status');
const errorBox = document.getElementById('error');
const controlsPanel = document.getElementById('controls');
const animSection = document.getElementById('anim-section');
const animSelect = document.getElementById('anim-select');
const animToggle = document.getElementById('anim-toggle');
const animLoop = document.getElementById('anim-loop');
const pickInfo = document.getElementById('pick-info');

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

const hemiLight = new THREE.HemisphereLight(0xffffff, 0x303848, 2.0);
scene.add(hemiLight);
const key = new THREE.DirectionalLight(0xffffff, 2.5);
key.position.set(4, 8, 5);
scene.add(key);
const fill = new THREE.DirectionalLight(0x9ab8ff, 1.0);
fill.position.set(-4, 2, -3);
scene.add(fill);

// Lighting adjustment sliders -- direct 1:1 with the light objects above,
// no indirection needed (three.js picks up intensity/exposure changes on
// the next render automatically, no needsUpdate call required).
document.getElementById('light-key').addEventListener('input', (e) => { key.intensity = Number(e.target.value); });
document.getElementById('light-fill').addEventListener('input', (e) => { fill.intensity = Number(e.target.value); });
document.getElementById('light-hemi').addEventListener('input', (e) => { hemiLight.intensity = Number(e.target.value); });
document.getElementById('light-exposure').addEventListener('input', (e) => { renderer.toneMappingExposure = Number(e.target.value); });

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

/*
 * Skeletal animation playback (gltf.animations, standard THREE.AnimationMixer
 * -- no husk-specific handling needed here, unlike the material-curve
 * extras above, since real glTF animation channels already carry this).
 * The dropdown/play-pause/loop controls are the concrete "doesn't play
 * animation back yet" gap TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md flagged.
 */
let mixer = null;
let clips = [];
let currentAction = null;
let paused = false;

function setupAnimationControls(gltf) {
  clips = gltf.animations || [];
  if (!clips.length) {
    animSection.classList.add('hidden');
    return;
  }
  mixer = new THREE.AnimationMixer(gltf.scene);
  animSelect.innerHTML = '';
  clips.forEach((clip, i) => {
    const opt = document.createElement('option');
    opt.value = String(i);
    opt.textContent = clip.name || `Clip ${i}`;
    animSelect.appendChild(opt);
  });
  animSection.classList.remove('hidden');
  playClip(0);
}

function playClip(index) {
  if (!mixer || !clips[index]) return;
  if (currentAction) currentAction.stop();
  currentAction = mixer.clipAction(clips[index]);
  currentAction.setLoop(animLoop.checked ? THREE.LoopRepeat : THREE.LoopOnce, Infinity);
  currentAction.clampWhenFinished = !animLoop.checked;
  currentAction.reset().play();
  paused = false;
  animToggle.textContent = 'Pause';
}

animSelect.addEventListener('change', () => playClip(Number(animSelect.value)));
animToggle.addEventListener('click', () => {
  paused = !paused;
  animToggle.textContent = paused ? 'Play' : 'Pause';
});
animLoop.addEventListener('change', () => {
  if (!currentAction) return;
  currentAction.setLoop(animLoop.checked ? THREE.LoopRepeat : THREE.LoopOnce, Infinity);
  currentAction.clampWhenFinished = !animLoop.checked;
});

/*
 * Picking: click a mesh (not a camera-orbit drag -- distinguished by
 * pointer movement between down/up, the standard technique since
 * OrbitControls itself doesn't expose a click-vs-drag signal) to inspect
 * its name, material, and the same husk extras (blend_mode/pixel_shader/
 * vertex_shader/texture_type) the Blender-side tooling already surfaces as
 * custom properties.
 */
const raycaster = new THREE.Raycaster();
const pointerNdc = new THREE.Vector2();
let pointerDownPos = null;

function extrasSummary(userData) {
  const lines = [];
  if (userData.blend_mode !== undefined) lines.push(`blend_mode: ${userData.blend_mode}`);
  if (userData.pixel_shader) lines.push(`pixel_shader: ${userData.pixel_shader}`);
  if (userData.vertex_shader) lines.push(`vertex_shader: ${userData.vertex_shader}`);
  if (userData.texture_type !== undefined) lines.push(`texture_type: ${userData.texture_type}`);
  if (userData.texture_file_data_id !== undefined) lines.push(`texture_file_data_id: ${userData.texture_file_data_id}`);
  return lines;
}

function onPointerDown(e) { pointerDownPos = { x: e.clientX, y: e.clientY }; }

function onPointerUp(e) {
  if (!pointerDownPos) return;
  const moved = Math.hypot(e.clientX - pointerDownPos.x, e.clientY - pointerDownPos.y);
  pointerDownPos = null;
  if (moved > 6) return;  // a drag, not a click -- let OrbitControls own it

  pointerNdc.x = (e.clientX / window.innerWidth) * 2 - 1;
  pointerNdc.y = -(e.clientY / window.innerHeight) * 2 + 1;
  raycaster.setFromCamera(pointerNdc, camera);
  const hits = raycaster.intersectObjects(scene.children, true).filter((h) => h.object.isMesh);
  if (!hits.length) {
    pickInfo.classList.add('hidden');
    return;
  }
  const obj = hits[0].object;
  const mat = Array.isArray(obj.material) ? obj.material[0] : obj.material;
  const lines = [`<b>${obj.name || '(unnamed mesh)'}</b>`];
  if (mat) {
    lines.push(`material: ${mat.name || '(unnamed)'}`);
    lines.push(...extrasSummary(mat.userData || {}));
  }
  pickInfo.innerHTML = lines.join('<br>');
  pickInfo.classList.remove('hidden');
}

renderer.domElement.addEventListener('pointerdown', onPointerDown);
renderer.domElement.addEventListener('pointerup', onPointerUp);

async function main() {
  try {
    const gltf = await loadEditableGLTF(MODEL_URL);
    applyEditableExtensions(gltf);
    scene.add(gltf.scene);
    frameObject(gltf.scene);
    collectMaterialCurveAnimations(gltf);

    setupAnimationControls(gltf);
    controlsPanel.classList.remove('hidden');

    const animations = gltf.animations?.length || 0;
    status.innerHTML = `<span style="color:var(--good)">loaded</span> · ` +
                       `${animations} animation${animations === 1 ? '' : 's'} · ` +
                       `${billboards.length} billboard${billboards.length === 1 ? '' : 's'} · ` +
                       `${materialCurveAnimations.length} material curve${materialCurveAnimations.length === 1 ? '' : 's'}`;
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

const clock = new THREE.Clock();
let materialCurveElapsed = 0;

function animate() {
  requestAnimationFrame(animate);
  const delta = clock.getDelta();
  if (!paused) {
    mixer?.update(delta);
    materialCurveElapsed += delta;
    updateMaterialCurveAnimations(materialCurveElapsed);
  }
  for (const billboard of billboards) billboardRotation(billboard);
  controls.update();
  renderer.render(scene, camera);
}

resize();
main();
animate();
