const $ = (id) => document.getElementById(id);

const stateEl = $("statusState");
const tMeasEl = $("tMeas");
const tSetEl = $("tSet");
const deltaEl = $("delta");
const dutyEl = $("duty");
const runSwitchEl = $("runSwitch");
const activeProfileEl = $("activeProfile");
const elapsedEl = $("elapsed");
const faultEl = $("fault");
const profileSelectEl = $("runProfile");
const profileListEl = $("profileList");
const profileNameEl = $("profileName");
const endBehaviorEl = $("endBehavior");
const pointsTableBody = $("pointsTable").querySelector("tbody");
const chartEl = $("profileChart");
const chartCtx = chartEl.getContext("2d");
const downloadBtn = $("downloadProfile");
const uploadInput = $("uploadProfile");

const toastEl = $("toast");
const themeToggleEl = $("themeToggle");

let lastStatus = null;
let measuredSeries = [];

const setCookie = (name, value, days = 365) => {
  const expires = new Date(Date.now() + days * 864e5).toUTCString();
  document.cookie = `${name}=${value}; expires=${expires}; path=/`;
};

const getCookie = (name) => {
  const match = document.cookie.match(new RegExp(`(^| )${name}=([^;]+)`));
  return match ? match[2] : "";
};

const applyTheme = (mode) => {
  document.body.classList.toggle("light", mode === "light");
  themeToggleEl.textContent = mode === "light" ? "Dark" : "Light";
};

const showToast = (msg) => {
  toastEl.textContent = msg;
  toastEl.classList.add("show");
  setTimeout(() => toastEl.classList.remove("show"), 2000);
};

const api = async (path, options = {}) => {
  const res = await fetch(path, options);
  const text = await res.text();
  try {
    return { ok: res.ok, status: res.status, data: JSON.parse(text) };
  } catch {
    return { ok: res.ok, status: res.status, data: text };
  }
};

const format = (value, digits = 2) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  return Number(value).toFixed(digits);
};

const updateStatus = async () => {
  const res = await api("/api/status");
  if (!res.ok || !res.data.ok) return;
  const s = res.data.data;
  lastStatus = s;
  stateEl.textContent = s.state;
  tMeasEl.textContent = `${format(s.t_meas)} C`;
  tSetEl.textContent = `${format(s.t_set)} C`;
  deltaEl.textContent = `${format(s.delta)} C`;
  dutyEl.textContent = format(s.duty, 3);
  runSwitchEl.textContent = s.run_switch ? "EN" : "DIS";
  activeProfileEl.textContent = s.active_profile || "-";
  elapsedEl.textContent = `${s.elapsed_sec ?? 0} sec`;
  faultEl.textContent = s.fault;
  updateMeasuredSeries();
  drawChart();
};

const addPointRow = (t = "", temp = "") => {
  const row = document.createElement("tr");
  row.innerHTML = `
    <td><input type="number" min="0" step="1" value="${t}"></td>
    <td><input type="number" step="0.1" value="${temp}"></td>
    <td><button class="btn ghost">Remove</button></td>
  `;
  row.querySelector("button").addEventListener("click", () => row.remove());
  row.querySelectorAll("input").forEach((input) =>
    input.addEventListener("input", () => drawChart())
  );
  pointsTableBody.appendChild(row);
};

const clearEditor = () => {
  profileNameEl.value = "";
  endBehaviorEl.value = "hold_last";
  pointsTableBody.innerHTML = "";
  measuredSeries = [];
  drawChart();
};

const collectPoints = () => {
  const points = [];
  pointsTableBody.querySelectorAll("tr").forEach((row) => {
    const t = row.querySelectorAll("input")[0].value;
    const temp = row.querySelectorAll("input")[1].value;
    if (t === "" || temp === "") return;
    points.push({ t_sec: Number(t), temp_c: Number(temp) });
  });
  return points;
};

const updateMeasuredSeries = () => {
  if (!lastStatus) return;
  const name = (profileNameEl.value || "").trim();
  if (!name || name !== lastStatus.active_profile) {
    measuredSeries = [];
    return;
  }
  const elapsed = Number(lastStatus.elapsed_sec ?? 0);
  const temp = Number(lastStatus.t_meas);
  if (!Number.isFinite(elapsed) || !Number.isFinite(temp)) return;
  if (elapsed === 0) {
    measuredSeries = [];
  }
  const last = measuredSeries[measuredSeries.length - 1];
  if (!last || last.t_sec !== elapsed) {
    measuredSeries.push({ t_sec: elapsed, temp_c: temp });
  }
};

const drawChart = () => {
  const points = collectPoints()
    .filter((p) => Number.isFinite(p.t_sec) && Number.isFinite(p.temp_c))
    .sort((a, b) => a.t_sec - b.t_sec);
  const width = chartEl.width;
  const height = chartEl.height;
  chartCtx.clearRect(0, 0, width, height);

  chartCtx.fillStyle = "rgba(255,255,255,0.02)";
  chartCtx.fillRect(0, 0, width, height);

  if (points.length === 0) return;
  const maxT = Math.max(...points.map((p) => p.t_sec), 1);
  const minTemp = Math.min(...points.map((p) => p.temp_c));
  const maxTemp = Math.max(...points.map((p) => p.temp_c));
  const spanTemp = Math.max(maxTemp - minTemp, 1);

  const pad = 30;
  const xFor = (t) => pad + (t / maxT) * (width - pad * 2);
  const yFor = (temp) => height - pad - ((temp - minTemp) / spanTemp) * (height - pad * 2);

  chartCtx.strokeStyle = "rgba(148,163,184,0.4)";
  chartCtx.lineWidth = 1;
  chartCtx.beginPath();
  chartCtx.moveTo(pad, pad);
  chartCtx.lineTo(pad, height - pad);
  chartCtx.lineTo(width - pad, height - pad);
  chartCtx.stroke();

  chartCtx.strokeStyle = "#f97316";
  chartCtx.lineWidth = 2;
  chartCtx.beginPath();
  points.forEach((p, i) => {
    const x = xFor(p.t_sec);
    const y = yFor(p.temp_c);
    if (i === 0) chartCtx.moveTo(x, y);
    else chartCtx.lineTo(x, y);
  });
  chartCtx.stroke();

  if (lastStatus && lastStatus.active_profile &&
      lastStatus.active_profile === (profileNameEl.value || "").trim()) {
    const elapsed = Number(lastStatus.elapsed_sec ?? 0);
    if (Number.isFinite(elapsed)) {
      for (let i = 1; i < points.length; i += 1) {
        const a = points[i - 1];
        const b = points[i];
        if (elapsed >= a.t_sec && elapsed <= b.t_sec) {
          chartCtx.strokeStyle = "#22d3ee";
          chartCtx.lineWidth = 4;
          chartCtx.beginPath();
          chartCtx.moveTo(xFor(a.t_sec), yFor(a.temp_c));
          chartCtx.lineTo(xFor(b.t_sec), yFor(b.temp_c));
          chartCtx.stroke();
          break;
        }
      }
    }
  }

  chartCtx.fillStyle = "#fb923c";
  points.forEach((p) => {
    const x = xFor(p.t_sec);
    const y = yFor(p.temp_c);
    chartCtx.beginPath();
    chartCtx.arc(x, y, 4, 0, Math.PI * 2);
    chartCtx.fill();
  });

  if (measuredSeries.length > 0) {
    chartCtx.fillStyle = "#38bdf8";
    measuredSeries.forEach((p) => {
      const x = xFor(p.t_sec);
      const y = yFor(p.temp_c);
      chartCtx.beginPath();
      chartCtx.arc(x, y, 3, 0, Math.PI * 2);
      chartCtx.fill();
    });
  }
};

const refreshProfiles = async () => {
  const res = await api("/api/profiles");
  if (!res.ok) return;
  profileListEl.innerHTML = "";
  profileSelectEl.innerHTML = `<option value="">(none)</option>`;
  (res.data.profiles || []).forEach((p) => {
    const item = document.createElement("div");
    item.className = "list-item";
    item.innerHTML = `
      <div>
        <div>${p.name}</div>
        <div class="hint">${p.points} points | ${p.end_behavior}</div>
      </div>
      <div class="row">
        <button class="btn" data-action="load">Load</button>
        <button class="btn ghost" data-action="delete">Delete</button>
      </div>
    `;
    item.querySelector('[data-action="load"]').addEventListener("click", () => loadProfile(p.name));
    item.querySelector('[data-action="delete"]').addEventListener("click", () => deleteProfile(p.name));
    profileListEl.appendChild(item);

    const opt = document.createElement("option");
    opt.value = p.name;
    opt.textContent = p.name;
    profileSelectEl.appendChild(opt);
  });
};

const loadProfile = async (name) => {
  const res = await api(`/api/profiles/${encodeURIComponent(name)}`);
  if (!res.ok) {
    showToast("Load failed");
    return;
  }
  clearEditor();
  profileNameEl.value = res.data.name;
  endBehaviorEl.value = res.data.end_behavior || "hold_last";
  (res.data.points || []).forEach((p) => addPointRow(p.t_sec, p.temp_c));
  measuredSeries = [];
  drawChart();
};

const deleteProfile = async (name) => {
  const res = await api(`/api/profiles/${encodeURIComponent(name)}`, { method: "DELETE" });
  if (!res.ok) {
    showToast("Delete failed");
    return;
  }
  showToast("Deleted");
  refreshProfiles();
};

const saveProfile = async () => {
  const name = profileNameEl.value.trim();
  if (!name) {
    showToast("Name required");
    return;
  }
  const points = collectPoints();
  if (points.length < 2) {
    showToast("Need 2+ points");
    return;
  }
  const payload = {
    name,
    end_behavior: endBehaviorEl.value,
    points,
  };
  const res = await api("/api/profiles", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok || res.data.ok === false) {
    showToast(`Save failed: ${res.data.error || res.status}`);
    return;
  }
  showToast("Saved");
  refreshProfiles();
  drawChart();
};

const downloadProfile = async () => {
  const name = profileNameEl.value.trim();
  if (!name) {
    showToast("Name required");
    return;
  }
  const res = await api(`/api/profiles/${encodeURIComponent(name)}`);
  if (!res.ok) {
    showToast("Download failed");
    return;
  }
  const blob = new Blob([JSON.stringify(res.data, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${name}.json`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
};

const uploadProfile = async (file) => {
  const text = await file.text();
  let payload;
  try {
    payload = JSON.parse(text);
  } catch {
    showToast("Invalid JSON");
    return;
  }
  if (!payload || !payload.name || !payload.points) {
    showToast("Missing name/points");
    return;
  }
  const res = await api("/api/profiles", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok || res.data.ok === false) {
    showToast(`Upload failed: ${res.data.error || res.status}`);
    return;
  }
  showToast("Uploaded");
  refreshProfiles();
  loadProfile(payload.name);
};

const runProfile = async () => {
  const profile_id = profileSelectEl.value;
  const payload = profile_id ? { profile_id } : {};
  const res = await api("/api/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok || res.data.ok === false) {
    showToast(`Run failed: ${res.data.error || res.status}`);
    return;
  }
  showToast("Running");
};

const stopRun = async () => {
  const res = await api("/api/stop", { method: "POST" });
  if (!res.ok || res.data.ok === false) {
    showToast("Stop failed");
    return;
  }
  showToast("Stopped");
};

$("addPoint").addEventListener("click", () => addPointRow());
$("saveProfile").addEventListener("click", saveProfile);
$("clearEditor").addEventListener("click", clearEditor);
$("runBtn").addEventListener("click", runProfile);
$("stopBtn").addEventListener("click", stopRun);
downloadBtn.addEventListener("click", downloadProfile);
uploadInput.addEventListener("change", (e) => {
  const file = e.target.files && e.target.files[0];
  if (file) uploadProfile(file);
  uploadInput.value = "";
});
themeToggleEl.addEventListener("click", () => {
  const next = document.body.classList.contains("light") ? "dark" : "light";
  applyTheme(next);
  setCookie("theme", next);
});

clearEditor();
refreshProfiles();
updateStatus();
setInterval(updateStatus, 1000);
drawChart();

const savedTheme = getCookie("theme");
if (savedTheme === "light" || savedTheme === "dark") {
  applyTheme(savedTheme);
} else {
  applyTheme("dark");
}
