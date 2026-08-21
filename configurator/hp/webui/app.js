/* HapticPad configurator - browser front end.
 *
 * All device access goes through the local Python server; this file never
 * touches a serial port itself.  Every request carries the token the server
 * injected into the page, which is what keeps some other page you have open
 * from driving your keyboard.
 */

"use strict";

const TOKEN = window.HP_TOKEN;

const ACTION_CATALOGUE = [
  ["Clipboard",  ["CTRL+C", "CTRL+V", "CTRL+X", "CTRL+Z", "CTRL+Y"]],
  ["Files",      ["CTRL+S", "CTRL+O", "CTRL+N", "CTRL+P", "CTRL+W", "CTRL+Q"]],
  ["Editing",    ["CTRL+A", "CTRL+F", "CTRL+B", "CTRL+I", "CTRL+U",
                  "DELETE", "BACKSPACE", "ENTER", "TAB", "ESC"]],
  ["Navigation", ["UP", "DOWN", "LEFT", "RIGHT", "HOME", "END", "PGUP", "PGDN",
                  "ALT+LEFT", "ALT+RIGHT", "CTRL+TAB", "CTRL+SHIFT+TAB"]],
  ["Function",   Array.from({length: 12}, (_, i) => "F" + (i + 1))],
  ["Media",      ["VOL_UP", "VOL_DOWN", "MUTE", "PLAY", "NEXT", "PREV",
                  "BRIGHT_UP", "BRIGHT_DOWN"]],
  ["Mouse",      ["MOUSE_LEFT", "MOUSE_RIGHT", "MOUSE_MIDDLE",
                  "SCROLL_UP", "SCROLL_DOWN", "SCROLL_LEFT", "SCROLL_RIGHT"]],
  ["Advanced",   ["TEXT:type this", "HOLD:CTRL", "RELEASE", "WAIT"]],
];

const SETTING_FIELDS = {
  led: [
    ["led_mode", "Mode", "select",
     ["off", "solid", "rainbow", "breath", "bands", "halo", "reactive", "profile"],
     "'profile' follows each profile's own colour"],
    ["led_brightness", "Brightness", "number", [0, 255, 1], "0 to 255"],
    ["led_primary", "Primary colour", "colour", null, "#RRGGBB"],
    ["led_secondary", "Secondary colour", "colour", null, "used by 'bands'"],
  ],
  wheel: [
    ["haptic_strength", "Force", "number", [0.2, 5, 0.2], "how hard the detents pull"],
    ["haptic_detents", "Detents per turn", "number", [2, 100, 1],
     "default when a profile does not say"],
    ["encoder_invert", "Invert direction", "select", ["0", "1"],
     "set to 1 if the wheel turns the wrong way"],
  ],
  display: [
    ["contrast", "Contrast", "number", [0, 255, 1], "0 to 255"],
    ["display_flip", "Rotate 180 degrees", "select", ["0", "1"], "for an upside-down panel"],
    ["screensaver_sec", "Screensaver", "number", [0, 900, 30],
     "seconds of inactivity; 0 disables it"],
  ],
  system: [
    ["log_level", "Log level", "select", ["error", "warn", "info", "debug"], ""],
  ],
};

const STATUS_FIELDS = [
  ["foc_hz", "FOC loop"], ["foc_ready", "FOC ready"], ["foc_fault", "Fault"],
  ["angle", "Wheel angle"], ["velocity", "Wheel speed"], ["voltage_q", "Motor Uq"],
  ["ticks_cw", "Detents CW"], ["ticks_ccw", "Detents CCW"], ["ticks_lost", "Detents lost"],
  ["hid_ready", "USB ready"], ["hid_queued", "HID queue"], ["hid_dropped", "HID dropped"],
  ["log_dropped", "Log dropped"], ["cdc_dropped", "Serial dropped"],
  ["uptime_ms", "Uptime (ms)"],
];

/* --- state --------------------------------------------------------- */

const state = {
  connected: false,
  deviceSettings: {},
  vocabulary: null,
  profiles: [],
  selectedProfile: null,
  profile: null,
  icons: [],
  selectedIcon: null,
  pendingIcon: null,
  previewTimer: null,
};

const $ = (id) => document.getElementById(id);

/* --- server calls -------------------------------------------------- */

async function api(path, body) {
  const options = {
    method: body === undefined ? "GET" : "POST",
    headers: {"X-HapticPad-Token": TOKEN},
  };
  if (body !== undefined) {
    options.headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(body);
  }

  const response = await fetch(path, options);
  const text = await response.text();

  let payload;
  try {
    payload = JSON.parse(text);
  } catch (err) {
    throw new Error("the server said: " + text.slice(0, 200));
  }
  if (!response.ok || payload.error) {
    throw new Error(payload.error || ("HTTP " + response.status));
  }
  return payload;
}

function say(message, isError) {
  const bar = $("statusbar");
  bar.textContent = message;
  bar.classList.toggle("error", !!isError);
}

/* Runs a device call and reports failure in the status bar rather than
 * throwing into the console where nobody looks. */
async function guarded(fn, successMessage) {
  try {
    const result = await fn();
    if (successMessage) say(successMessage);
    return result;
  } catch (err) {
    say(err.message, true);
    appendLog("! " + err.message);
    return null;
  }
}

/* --- connection ---------------------------------------------------- */

async function refreshPorts() {
  const data = await guarded(() => api("/api/ports"));
  if (!data) return;

  const select = $("port-select");
  select.innerHTML = "";
  for (const port of data.ports) {
    const option = document.createElement("option");
    option.value = port.device;
    option.textContent = port.device +
      (port.description ? "  (" + port.description + ")" : "") +
      (port.likely ? "  ← likely" : "");
    select.appendChild(option);
  }

  if (!data.ports.length) {
    const option = document.createElement("option");
    option.textContent = "no serial ports found";
    option.disabled = true;
    select.appendChild(option);
    say("No serial ports found - is the pad plugged in?");
  } else {
    say(data.ports.length + " serial port(s) found");
  }
}

async function toggleConnection() {
  if (state.connected) {
    await guarded(() => api("/api/disconnect", {}), "Disconnected");
    setConnected(false, "");
    return;
  }

  const port = $("port-select").value;
  if (!port) { say("Choose a port first."); return; }

  const data = await guarded(() => api("/api/connect", {port}));
  if (!data) return;

  setConnected(true, data.greeting);
  appendLog("connected to " + data.device + " - " + data.greeting);

  await reloadProfiles();
  await reloadIcons();
  await readSettings();
  say("Connected");
}

function setConnected(connected, greeting) {
  state.connected = connected;
  $("btn-connect").textContent = connected ? "Disconnect" : "Connect";
  const label = $("device-label");
  label.textContent = connected ? greeting : "not connected";
  label.classList.toggle("connected", connected);
  label.classList.toggle("muted", !connected);
}

/* --- profiles ------------------------------------------------------ */

async function reloadProfiles() {
  const data = await guarded(() => api("/api/profiles"));
  if (!data) return;

  state.profiles = data.profiles;
  const list = $("profile-list");
  list.innerHTML = "";

  for (const entry of data.profiles) {
    const item = document.createElement("li");
    item.textContent = entry.file;
    item.dataset.file = entry.file;
    if (entry.active) {
      const badge = document.createElement("span");
      badge.className = "badge";
      badge.textContent = "active";
      item.appendChild(badge);
    }
    item.addEventListener("click", () => selectProfile(entry.file));
    list.appendChild(item);
  }

  if (data.profiles.length) {
    await selectProfile(state.selectedProfile &&
      data.profiles.some(p => p.file === state.selectedProfile)
        ? state.selectedProfile : data.profiles[0].file);
  }
  say(data.profiles.length + " profile(s) on the card");
}

async function selectProfile(file) {
  const data = await guarded(() => api("/api/profile?file=" + encodeURIComponent(file)));
  if (!data) return;

  state.selectedProfile = file;
  state.profile = data.profile;

  for (const item of $("profile-list").children) {
    item.classList.toggle("selected", item.dataset.file === file);
  }
  profileToForm();
}

function profileToForm() {
  const p = state.profile;
  $("p-filename").value = p.filename || "";
  $("p-name").value = p.name || "";
  $("p-led-mode").value = p.led_mode || "";
  $("p-led-primary").value = p.led_primary || "";
  $("p-led-secondary").value = p.led_secondary || "";
  $("p-led-brightness").value = p.led_brightness || "";
  $("p-wheel").value = p.wheel || "clicky";
  $("p-wheel-func").value = p.wheel_func || "scroll";
  $("p-detents").value = p.detents || "";
  $("p-strength").value = p.strength || "";
  $("p-cw").value = p.wheel_cw || "";
  $("p-ccw").value = p.wheel_ccw || "";

  syncColourPickers();
  updateLedRing();
  updateWheelHelp();
  renderButtonCards();
  schedulePreview();
}

function formToProfile() {
  const p = state.profile;
  if (!p) return null;

  p.filename = $("p-filename").value.trim();
  p.name = $("p-name").value.trim();
  p.led_mode = $("p-led-mode").value;
  p.led_primary = $("p-led-primary").value.trim();
  p.led_secondary = $("p-led-secondary").value.trim();
  p.led_brightness = $("p-led-brightness").value.trim();
  p.wheel = $("p-wheel").value;
  p.wheel_func = $("p-wheel-func").value;
  p.detents = $("p-detents").value.trim();

  const strength = $("p-strength").value.trim();
  p.strength = (strength === "" || Number(strength) === 0) ? "" : strength;

  p.wheel_cw = $("p-cw").value.trim();
  p.wheel_ccw = $("p-ccw").value.trim();

  p.buttons.forEach((button, index) => {
    button.label = $("k-label-" + index).value.trim();
    button.icon = $("k-icon-" + index).value.trim();
    const text = $("k-action-" + index).value.trim();
    if (text !== button.action) {
      // Typed by hand: let the server re-split it into steps on save.
      button.action = text;
      button.steps = null;
    }
  });
  return p;
}

function renderButtonCards() {
  const grid = $("button-grid");
  grid.innerHTML = "";

  state.profile.buttons.forEach((button, index) => {
    const card = document.createElement("div");
    card.className = "key-card";
    card.innerHTML = `
      <h3>Button ${index + 1}</h3>
      <div class="grid">
        <label>Label<input id="k-label-${index}" type="text" maxlength="11"></label>
        <label>Icon<select id="k-icon-${index}"></select></label>
      </div>
      <label class="full">Macro
        <span class="with-button">
          <input id="k-action-${index}" type="text" placeholder="CTRL+S">
          <button class="ghost small" id="k-steps-${index}">Steps</button>
          <button class="ghost small" id="k-test-${index}">Test</button>
        </span>
      </label>`;
    grid.appendChild(card);

    $("k-label-" + index).value = button.label || "";
    $("k-action-" + index).value = button.action || "";
    fillIconSelect($("k-icon-" + index), button.icon);

    $("k-label-" + index).addEventListener("input", schedulePreview);
    $("k-icon-" + index).addEventListener("change", schedulePreview);
    $("k-steps-" + index).addEventListener("click", (e) => {
      e.preventDefault();
      openStepEditor(index);
    });
    $("k-test-" + index).addEventListener("click", async (e) => {
      e.preventDefault();
      formToProfile();
      const text = $("k-action-" + index).value.trim();
      for (const part of text.split(";")) {
        const action = part.split(",")[0].trim();
        if (action) await guarded(() => api("/api/test", {action}));
      }
      say("Sent button " + (index + 1));
    });
  });
}

function fillIconSelect(select, current) {
  select.innerHTML = "";
  const none = document.createElement("option");
  none.value = "";
  none.textContent = "(none)";
  select.appendChild(none);

  const names = state.icons.slice();
  if (current && !names.includes(current)) names.push(current + "  (missing)");

  for (const name of names) {
    const option = document.createElement("option");
    option.value = name.replace(/\s+\(missing\)$/, "");
    option.textContent = name;
    select.appendChild(option);
  }
  select.value = current || "";
}

function updateWheelHelp() {
  const func = $("p-wheel-func").value;
  const entry = (state.vocabulary.wheel_funcs || []).find(f => f.name === func);
  $("wheel-help").textContent = entry
    ? (entry.help + (entry.cw ? "   — preset: " + entry.cw + " / " + entry.ccw : ""))
    : "";
}

/* Accepts #RRGGBB, 0xRRGGBB or bare RRGGBB - all three turn up in profiles
 * written by hand or by the old firmware. */
function normaliseColour(text) {
  const raw = (text || "").trim().replace(/^#/, "").replace(/^0x/i, "");
  return /^[0-9a-f]{6}$/i.test(raw) ? "#" + raw.toUpperCase() : null;
}

function syncColourPickers() {
  for (const field of ["p-led-primary", "p-led-secondary"]) {
    const colour = normaliseColour($(field).value);
    if (colour) $(field + "-picker").value = colour.toLowerCase();
  }
}

/* Whatever the ring will actually do: the profile where it says so, the
 * device settings everywhere else. */
function effectiveLed() {
  const device = state.deviceSettings || {};
  return {
    mode: $("p-led-mode").value || device.led_mode || "rainbow",
    primary: normaliseColour($("p-led-primary").value) ||
             normaliseColour(device.led_primary) || "#00FF00",
    secondary: normaliseColour($("p-led-secondary").value) ||
               normaliseColour(device.led_secondary) || "#0000FF",
    brightness: Number($("p-led-brightness").value !== ""
      ? $("p-led-brightness").value
      : (device.led_brightness !== undefined ? device.led_brightness : 128)),
    inherits: {
      mode: !$("p-led-mode").value,
      primary: !$("p-led-primary").value.trim(),
      secondary: !$("p-led-secondary").value.trim(),
      brightness: $("p-led-brightness").value.trim() === "",
    },
  };
}

function hexToRgb(hex) {
  const value = parseInt(hex.slice(1), 16);
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

function scaleRgb([r, g, b], factor) {
  const clamp = (v) => Math.max(0, Math.min(255, Math.round(v)));
  return `rgb(${clamp(r * factor)}, ${clamp(g * factor)}, ${clamp(b * factor)})`;
}

function hueToRgb(hue) {
  const region = Math.floor(hue / 43);
  const remainder = (hue - region * 43) * 6;
  const q = 255 - remainder, t = remainder;
  switch (region) {
    case 0: return [255, t, 0];
    case 1: return [q, 255, 0];
    case 2: return [0, 255, t];
    case 3: return [0, q, 255];
    case 4: return [t, 0, 255];
    default: return [255, 0, q];
  }
}

/* A still frame of what the ring will look like.  Animated modes are shown at
 * a representative instant - enough to tell breath from bands at a glance. */
function updateLedRing() {
  const led = effectiveLed();
  const canvas = $("led-ring");
  const context = canvas.getContext("2d");
  const count = 20;
  const centre = canvas.width / 2;
  const radius = centre - 12;

  context.clearRect(0, 0, canvas.width, canvas.height);

  const primary = hexToRgb(led.primary);
  const secondary = hexToRgb(led.secondary);
  const level = Math.max(0.08, led.brightness / 255);

  for (let i = 0; i < count; i++) {
    const angle = (i / count) * Math.PI * 2 - Math.PI / 2;
    const x = centre + Math.cos(angle) * radius;
    const y = centre + Math.sin(angle) * radius;

    let colour;
    switch (led.mode) {
      case "off":      colour = "rgb(20, 24, 28)"; break;
      case "rainbow":  colour = scaleRgb(hueToRgb(Math.round(i * 256 / count)), level); break;
      case "bands":    colour = scaleRgb(i < count / 2 ? primary : secondary, level); break;
      case "breath":   colour = scaleRgb(primary, level * 0.55); break;
      case "halo": {
        const distance = Math.min(i, count - i);
        colour = scaleRgb(primary, level * (distance >= 4 ? 0.05 : 1 - distance * 0.23));
        break;
      }
      case "reactive": colour = scaleRgb(primary, level * (0.2 + 0.8 * (i / count))); break;
      default:         colour = scaleRgb(primary, level); break;   // solid, profile
    }

    context.beginPath();
    context.arc(x, y, 5, 0, Math.PI * 2);
    context.fillStyle = colour;
    context.fill();
  }

  const inherited = Object.entries(led.inherits)
    .filter(([, yes]) => yes)
    .map(([name]) => name);

  const entry = (state.vocabulary.led_modes || []).find(m => m.name === led.mode);
  const description = entry ? entry.help : led.mode;

  $("led-help").innerHTML = description +
    (inherited.length
      ? '<br><span class="led-inherited">from the device settings: ' +
        inherited.join(", ") + "</span>"
      : '<br><span class="led-inherited">this profile sets the whole ring</span>');
}

/* Preview rendering is a server round trip, so coalesce keystrokes. */
function schedulePreview() {
  clearTimeout(state.previewTimer);
  state.previewTimer = setTimeout(renderPreview, 180);
}

async function renderPreview() {
  const profile = formToProfile();
  if (!profile) return;

  const index = state.profiles.findIndex(p => p.file === profile.filename);
  const data = await api("/api/preview", {
    profile,
    index: index >= 0 ? index : null,
    total: state.profiles.length || null,
  }).catch(() => null);
  if (!data) return;

  drawMatrix($("oled"), data.rows, 3);
}

function drawMatrix(canvas, rows, scale) {
  const context = canvas.getContext("2d");
  const styles = getComputedStyle(document.documentElement);
  canvas.width = rows[0].length * scale;
  canvas.height = rows.length * scale;

  context.fillStyle = styles.getPropertyValue("--oled-off").trim() || "#08131a";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.fillStyle = styles.getPropertyValue("--oled-on").trim() || "#3fe0e8";

  rows.forEach((row, y) => {
    let x = 0;
    while (x < row.length) {
      if (row[x] === "0") { x++; continue; }
      const start = x;
      while (x < row.length && row[x] === "1") x++;
      context.fillRect(start * scale, y * scale, (x - start) * scale, scale);
    }
  });
}

/* --- step editor --------------------------------------------------- */

let stepEditorIndex = null;

function openStepEditor(index) {
  formToProfile();
  stepEditorIndex = index;

  const button = state.profile.buttons[index];
  $("modal-title").textContent =
    "Button " + (index + 1) + (button.label ? " — " + button.label : "");

  const rows = $("step-rows");
  rows.innerHTML = "";

  const steps = button.steps && button.steps.length
    ? button.steps
    : splitActionText(button.action);
  if (!steps.length) steps.push({action: "", delay: 0});
  steps.forEach(step => addStepRow(step.action, step.delay));

  $("modal").classList.remove("hidden");
}

/* Mirrors the firmware's own splitting so the editor shows what the pad
 * would actually do with a hand-typed macro. */
function splitActionText(text) {
  const out = [];
  for (const segment of (text || "").split(";")) {
    const trimmed = segment.trim();
    if (!trimmed) continue;

    let action = trimmed, delay = 0;
    if (!/^TEXT:/i.test(trimmed)) {
      const comma = trimmed.lastIndexOf(",");
      if (comma > 0 && /^\s*\d+\s*$/.test(trimmed.slice(comma + 1))) {
        action = trimmed.slice(0, comma).trim();
        delay = parseInt(trimmed.slice(comma + 1), 10);
      }
    }
    out.push({action, delay});
  }
  return out;
}

function addStepRow(action, delay) {
  const rows = $("step-rows");
  if (rows.children.length >= state.vocabulary.max_steps) {
    say("A button holds at most " + state.vocabulary.max_steps + " steps.");
    return;
  }

  const row = document.createElement("tr");
  row.innerHTML = `
    <td><input type="text" class="step-action" value=""></td>
    <td><input type="number" class="step-delay" min="0" max="60000" value="0"></td>
    <td><button class="ghost small step-test">Test</button>
        <button class="ghost small danger step-remove">Remove</button></td>`;
  rows.appendChild(row);

  row.querySelector(".step-action").value = action || "";
  row.querySelector(".step-delay").value = delay || 0;

  row.querySelector(".step-test").addEventListener("click", (e) => {
    e.preventDefault();
    const value = row.querySelector(".step-action").value.trim();
    if (value) guarded(() => api("/api/test", {action: value}), "Sent " + value);
  });
  row.querySelector(".step-remove").addEventListener("click", (e) => {
    e.preventDefault();
    row.remove();
  });
}

function closeStepEditor(commit) {
  if (commit && stepEditorIndex !== null) {
    const steps = [];
    for (const row of $("step-rows").children) {
      const action = row.querySelector(".step-action").value.trim();
      if (!action) continue;
      steps.push({action, delay: parseInt(row.querySelector(".step-delay").value, 10) || 0});
    }
    const button = state.profile.buttons[stepEditorIndex];
    button.steps = steps;
    button.action = steps
      .map(s => s.delay ? s.action + "," + s.delay : s.action)
      .join(";");
    $("k-action-" + stepEditorIndex).value = button.action;
    schedulePreview();
  }
  stepEditorIndex = null;
  $("modal").classList.add("hidden");
}

/* --- saving -------------------------------------------------------- */

async function saveProfile(activate) {
  const profile = formToProfile();
  if (!profile) return;
  if (!profile.filename) { say("Give the profile a file name first.", true); return; }

  const data = await guarded(() => api("/api/profile/save", {profile}));
  if (!data) return;

  $("p-filename").value = data.filename;
  state.selectedProfile = data.filename;
  say("Saved " + data.filename + " (" + data.written + " bytes)");

  if (activate) {
    await guarded(() => api("/api/profile/activate", {file: data.filename}),
                  "Activated " + data.filename);
  }
  await reloadProfiles();
}

/* --- settings ------------------------------------------------------ */

function buildSettingsForm() {
  for (const [group, fields] of Object.entries(SETTING_FIELDS)) {
    const container = $("settings-" + group);
    container.innerHTML = "";

    for (const [key, label, kind, options, hint] of fields) {
      const row = document.createElement("div");
      row.className = "setting-row";

      let control;
      if (kind === "select") {
        control = `<select id="s-${key}">` +
          options.map(o => `<option value="${o}">${o}</option>`).join("") + "</select>";
      } else if (kind === "number") {
        const [min, max, step] = options;
        control = `<input id="s-${key}" type="number" min="${min}" max="${max}" step="${step}">`;
      } else {
        control = `<span class="colour-field">
            <input id="s-${key}" type="text">
            <input id="s-${key}-picker" type="color" aria-label="Pick colour"></span>`;
      }

      row.innerHTML = `<label>${label}${control}</label>
                       <button class="ghost" id="s-${key}-apply">Apply</button>` +
                      (hint ? `<span class="hint">${hint}</span>` : "");
      container.appendChild(row);

      $("s-" + key + "-apply").addEventListener("click", () => applySetting(key));

      const picker = $("s-" + key + "-picker");
      if (picker) {
        picker.addEventListener("input", () => { $("s-" + key).value = picker.value.toUpperCase(); });
      }
    }
  }
}

async function readSettings() {
  const data = await guarded(() => api("/api/settings"));
  if (!data) return;

  state.deviceSettings = data.settings;
  updateLedRing();

  for (const [key, value] of Object.entries(data.settings)) {
    const field = $("s-" + key);
    if (!field) continue;
    field.value = value;
    const picker = $("s-" + key + "-picker");
    if (picker && /^#?[0-9a-f]{6}$/i.test(value)) {
      picker.value = value.startsWith("#") ? value : "#" + value;
    }
  }
  say("Settings read from the pad");
}

async function applySetting(key) {
  const value = $("s-" + key).value.trim();
  await guarded(() => api("/api/settings", {key, value}), key + " = " + value);
  state.deviceSettings[key] = value;
  updateLedRing();
}

async function applyAllSettings() {
  for (const fields of Object.values(SETTING_FIELDS)) {
    for (const [key] of fields) {
      const value = $("s-" + key).value.trim();
      if (value !== "") await guarded(() => api("/api/settings", {key, value}));
    }
  }
  say("All settings applied");
}

/* --- icons --------------------------------------------------------- */

async function reloadIcons() {
  const data = await guarded(() => api("/api/icons"));
  if (!data) return;

  state.icons = data.icons;
  const list = $("icon-list");
  list.innerHTML = "";

  for (const name of data.icons) {
    const item = document.createElement("li");
    item.textContent = name;
    item.dataset.name = name;
    item.addEventListener("click", () => selectIcon(name));
    list.appendChild(item);
  }

  if (state.profile) {
    state.profile.buttons.forEach((button, index) => {
      const select = $("k-icon-" + index);
      if (select) fillIconSelect(select, button.icon);
    });
  }
  say(data.icons.length + " icon(s) in " + data.dir);
}

async function selectIcon(name) {
  const data = await guarded(() => api("/api/icon?name=" + encodeURIComponent(name)));
  if (!data) return;

  state.selectedIcon = name;
  state.pendingIcon = null;
  for (const item of $("icon-list").children) {
    item.classList.toggle("selected", item.dataset.name === name);
  }

  drawMatrix($("icon-canvas"), data.rows, 8);
  $("icon-name").value = name;
  $("icon-info").innerHTML = name + "<br>" +
    Math.round(data.coverage * 100) + "% of pixels lit" +
    (data.coverage < 0.01
      ? "<br><strong>This icon is blank</strong> - it will show as an empty cell."
      : "");
}

async function convertIcon() {
  const file = $("icon-file").files[0];
  if (!file) return;

  const buffer = await file.arrayBuffer();
  let binary = "";
  const bytes = new Uint8Array(buffer);
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);

  const data = await guarded(() => api("/api/icon/convert", {
    image: btoa(binary),
    mode: $("icon-mode").value,
    threshold: parseInt($("icon-threshold").value, 10),
  }));
  if (!data) return;

  state.pendingIcon = data.data;
  drawMatrix($("icon-canvas"), data.rows, 8);
  $("icon-info").innerHTML = file.name + "<br>read as: <strong>" + data.mode + "</strong><br>" +
    Math.round(data.coverage * 100) + "% of pixels lit" +
    (data.coverage < 0.01
      ? "<br><strong>Nothing is lit</strong> - try another interpretation or threshold."
      : "");

  if (!$("icon-name").value || state.icons.includes($("icon-name").value)) {
    $("icon-name").value = file.name.replace(/\.[^.]+$/, "") + ".bin";
  }
}

async function uploadIcon() {
  if (!state.pendingIcon) { say("Choose an image to convert first.", true); return; }
  const name = $("icon-name").value.trim();
  if (!name) { say("Give the icon a file name.", true); return; }

  const data = await guarded(() => api("/api/icon/upload", {name, data: state.pendingIcon}));
  if (!data) return;

  say("Uploaded " + data.name);
  await reloadIcons();
  schedulePreview();
}

/* --- monitor ------------------------------------------------------- */

function buildStatusTable() {
  const table = $("status-table");
  table.innerHTML = "";
  for (const [key, label] of STATUS_FIELDS) {
    const row = document.createElement("tr");
    row.innerHTML = `<td>${label}</td><td id="st-${key}">-</td>`;
    table.appendChild(row);
  }
}

function appendLog(line) {
  const log = $("log");
  log.textContent += line.replace(/\s+$/, "") + "\n";
  if (log.textContent.length > 200000) {
    log.textContent = log.textContent.slice(-120000);
  }
  log.scrollTop = log.scrollHeight;
}

async function poll() {
  if (state.connected) {
    const monitorVisible = $("panel-monitor").classList.contains("active");
    try {
      const data = await api(monitorVisible ? "/api/status" : "/api/logs");
      (data.logs || []).forEach(appendLog);
      if (data.status) {
        for (const [key] of STATUS_FIELDS) {
          const cell = $("st-" + key);
          if (cell) cell.textContent = data.status[key] !== undefined ? data.status[key] : "-";
        }
      }
    } catch (err) {
      appendLog("! link lost: " + err.message);
      setConnected(false, "");
    }
  }
  setTimeout(poll, 700);
}

/* --- wiring -------------------------------------------------------- */

function switchTab(name) {
  for (const tab of document.querySelectorAll(".tab")) {
    tab.classList.toggle("active", tab.dataset.tab === name);
  }
  for (const panel of document.querySelectorAll(".panel")) {
    panel.classList.toggle("active", panel.id === "panel-" + name);
  }
}

async function init() {
  state.vocabulary = await api("/api/vocabulary");

  for (const mode of state.vocabulary.wheel_modes) {
    $("p-wheel").add(new Option(mode, mode));
  }
  for (const entry of state.vocabulary.wheel_funcs) {
    $("p-wheel-func").add(new Option(entry.name, entry.name));
  }
  $("p-led-mode").add(new Option("(follow device setting)", ""));
  for (const entry of state.vocabulary.led_modes) {
    $("p-led-mode").add(new Option(entry.name, entry.name));
  }
  for (const mode of state.vocabulary.icon_modes) {
    $("icon-mode").add(new Option(mode, mode));
  }
  for (const [name] of ACTION_CATALOGUE) {
    $("step-category").add(new Option(name, name));
  }

  buildSettingsForm();
  buildStatusTable();
  refreshActionChoices();

  document.querySelectorAll(".tab").forEach(tab =>
    tab.addEventListener("click", () => switchTab(tab.dataset.tab)));

  $("btn-refresh-ports").addEventListener("click", refreshPorts);
  $("btn-connect").addEventListener("click", toggleConnection);

  ["p-name", "p-filename"].forEach(id =>
    $(id).addEventListener("input", schedulePreview));
  for (const field of ["p-led-primary", "p-led-secondary"]) {
    $(field).addEventListener("input", () => { syncColourPickers(); updateLedRing(); });
    $(field + "-picker").addEventListener("input", () => {
      $(field).value = $(field + "-picker").value.toUpperCase();
      updateLedRing();
    });
  }
  $("p-led-mode").addEventListener("change", updateLedRing);
  $("p-led-brightness").addEventListener("input", updateLedRing);

  // "Clear" means "say nothing and follow the device setting", which is not
  // something a colour picker can express on its own.
  document.querySelectorAll("[data-clear]").forEach(button =>
    button.addEventListener("click", (e) => {
      e.preventDefault();
      $(button.dataset.clear).value = "";
      updateLedRing();
    }));

  $("p-wheel-func").addEventListener("change", updateWheelHelp);

  document.querySelectorAll("[data-test]").forEach(button =>
    button.addEventListener("click", (e) => {
      e.preventDefault();
      const value = $(button.dataset.test).value.trim();
      if (value) guarded(() => api("/api/test", {action: value}), "Sent " + value);
    }));

  $("btn-profile-reload").addEventListener("click", reloadProfiles);
  $("btn-profile-save").addEventListener("click", () => saveProfile(false));
  $("btn-profile-save-activate").addEventListener("click", () => saveProfile(true));

  $("btn-profile-activate").addEventListener("click", () => {
    if (!state.selectedProfile) return;
    guarded(() => api("/api/profile/activate", {file: state.selectedProfile}),
            "Activated " + state.selectedProfile).then(reloadProfiles);
  });

  $("btn-profile-delete").addEventListener("click", async () => {
    if (!state.selectedProfile) return;
    if (!confirm("Delete " + state.selectedProfile + " from the card?")) return;
    await guarded(() => api("/api/profile/delete", {file: state.selectedProfile}),
                  "Deleted " + state.selectedProfile);
    state.selectedProfile = null;
    await reloadProfiles();
  });

  $("btn-profile-new").addEventListener("click", () => {
    state.profile = {
      filename: "new.txt", name: "New profile",
      led_mode: "", led_primary: "", led_secondary: "", led_brightness: "",
      wheel: "clicky", wheel_func: "scroll", detents: "", strength: "",
      wheel_cw: "", wheel_ccw: "", extra: [],
      buttons: Array.from({length: state.vocabulary.buttons}, (_, i) =>
        ({label: "Key" + (i + 1), icon: "", action: "", steps: []})),
    };
    profileToForm();
    say("New profile - set a file name, then Save to pad");
  });

  $("btn-profile-copy").addEventListener("click", () => {
    if (!state.profile) return;
    formToProfile();
    const base = (state.profile.filename || "profile").replace(/\.[^.]+$/, "");
    $("p-filename").value = base + "_copy.txt";
    say("Duplicated - Save to pad to write it");
  });

  $("btn-settings-read").addEventListener("click", readSettings);
  $("btn-settings-apply").addEventListener("click", applyAllSettings);
  $("btn-settings-save").addEventListener("click", () =>
    guarded(() => api("/api/settings/save", {}), "Settings written to the card"));

  $("btn-reboot").addEventListener("click", async () => {
    if (!confirm("Restart the pad?")) return;
    await guarded(() => api("/api/reboot", {}), "Restarting");
    setConnected(false, "");
  });

  $("btn-bootsel").addEventListener("click", async () => {
    if (!confirm("Put the pad into firmware update mode?\n\n" +
                 "It will disconnect and appear as a drive called RPI-RP2.")) return;
    await guarded(() => api("/api/bootsel", {}), "Bootloader");
    setConnected(false, "");
  });

  $("btn-icons-reload").addEventListener("click", reloadIcons);
  $("icon-file").addEventListener("change", convertIcon);
  $("icon-mode").addEventListener("change", convertIcon);
  $("icon-threshold").addEventListener("input", () => {
    $("icon-threshold-value").textContent = $("icon-threshold").value;
  });
  $("icon-threshold").addEventListener("change", convertIcon);
  $("btn-icon-upload").addEventListener("click", uploadIcon);
  $("btn-icon-delete").addEventListener("click", async () => {
    if (!state.selectedIcon) return;
    if (!confirm("Delete " + state.selectedIcon + " from the card?")) return;
    await guarded(() => api("/api/icon/delete", {name: state.selectedIcon}),
                  "Deleted " + state.selectedIcon);
    state.selectedIcon = null;
    await reloadIcons();
  });

  $("step-category").addEventListener("change", refreshActionChoices);
  $("btn-step-add").addEventListener("click", () => addStepRow($("step-action").value, 0));
  $("btn-step-ok").addEventListener("click", () => closeStepEditor(true));
  $("btn-step-cancel").addEventListener("click", () => closeStepEditor(false));
  $("modal").addEventListener("click", (e) => {
    if (e.target.id === "modal") closeStepEditor(false);
  });

  $("btn-log-clear").addEventListener("click", () => { $("log").textContent = ""; });
  $("btn-raw-send").addEventListener("click", sendRaw);
  $("raw-command").addEventListener("keydown", (e) => {
    if (e.key === "Enter") sendRaw();
  });

  $("icon-threshold-value").textContent = $("icon-threshold").value;

  await refreshPorts();
  poll();
}

function refreshActionChoices() {
  const name = $("step-category").value || ACTION_CATALOGUE[0][0];
  const entry = ACTION_CATALOGUE.find(([label]) => label === name);
  const select = $("step-action");
  select.innerHTML = "";
  for (const action of entry[1]) select.add(new Option(action, action));
}

async function sendRaw() {
  const input = $("raw-command");
  const command = input.value.trim();
  if (!command) return;

  appendLog("> " + command);
  const data = await guarded(() => api("/api/raw", {command}));
  if (!data) return;

  (data.data || []).forEach(line => appendLog("  " + line));
  appendLog((data.ok ? "OK " : "ERR ") + data.message);
  input.value = "";
}

init().catch(err => say("Startup failed: " + err.message, true));
