/**
 * FocusShield — Frontend Interactive Controller
 * Handles real-time synchronization, radial countdown timer,
 * domain previews, presets, and diagnostic verifications.
 */

// State
let appState = {
  shield_active: false,
  is_admin: false,
  total_active_hostnames: 0,
  system_blocked_count: 0,
  custom_domains: [],
  presets: {},
  focus_session: { active: false },
  stats: {},
  browser_doh: {},
  current_time: Math.floor(Date.now() / 1000),
};

let timerInterval = null;
let selectedTimerMinutes = 25;
let debounceTimer = null;
const RADIUS = 88;
const CIRCUMFERENCE = 2 * Math.PI * RADIUS; // ~552.92

// DOM Elements
const masterToggle = document.getElementById("master-shield-toggle");
const osStatusPill = document.getElementById("os-status-pill");
const osStatusText = document.getElementById("os-status-text");
const adminBadge = document.getElementById("admin-badge");
const adminText = document.getElementById("admin-text");
const btnElevate = document.getElementById("btn-elevate");

const metricBlockedDomains = document.getElementById("metric-blocked-domains");
const metricActiveHosts = document.getElementById("metric-active-hosts");
const masterHeroCard = document.querySelector(".master-hero-card");
const masterTitle = document.getElementById("master-title");
const masterSubtitle = document.getElementById("master-subtitle");

const timerRing = document.getElementById("timer-progress-ring");
const timerReadout = document.getElementById("timer-readout");
const timerSubtext = document.getElementById("timer-subtext");
const timerStatusBadge = document.getElementById("timer-status-badge");
const btnStartTimer = document.getElementById("btn-start-timer");
const btnStopTimer = document.getElementById("btn-stop-timer");
const strictModeToggle = document.getElementById("strict-mode-toggle");
const timerPresetsGroup = document.getElementById("timer-presets-group");

const addForm = document.getElementById("add-domain-form");
const domainInput = document.getElementById("domain-input");
const domainPreviewBanner = document.getElementById("domain-preview-banner");
const previewCount = document.getElementById("preview-count");
const previewHostsChips = document.getElementById("preview-hosts-chips");

const presetsGrid = document.getElementById("presets-grid");
const domainsList = document.getElementById("domains-list");
const blocklistSearch = document.getElementById("blocklist-search");

const diagInput = document.getElementById("diag-input");
const btnRunDiag = document.getElementById("btn-run-diag");
const diagResultBox = document.getElementById("diag-result-box");

const btnToggleDoh = document.getElementById("btn-toggle-doh");
const dohBtnText = document.getElementById("doh-btn-text");
const syncTimestamp = document.getElementById("sync-timestamp");

// Modal Elements
const pledgeModal = document.getElementById("pledge-modal");
const pledgeInput = document.getElementById("pledge-input");
const btnPledgeConfirm = document.getElementById("btn-pledge-confirm");
const btnPledgeCancel = document.getElementById("btn-pledge-cancel");
const EXPECTED_PLEDGE = "i choose long-term focus over short-term distraction";

// ==========================================
// Initialization
// ==========================================
document.addEventListener("DOMContentLoaded", () => {
  // Setup radial ring
  if (timerRing) {
    timerRing.style.strokeDasharray = `${CIRCUMFERENCE}`;
    timerRing.style.strokeDashoffset = "0";
  }

  setupEventListeners();
  fetchStatus();
  // Poll status every 4 seconds
  setInterval(fetchStatus, 4000);
});

// ==========================================
// API Handlers
// ==========================================
async function fetchStatus() {
  try {
    const res = await fetch("/api/status");
    if (!res.ok) throw new Error("Status fetch error");
    const data = await res.json();
    appState = data;
    renderAll();
  } catch (err) {
    console.warn("Could not reach backend server:", err);
  }
}

async function apiPost(endpoint, payload = {}) {
  try {
    const res = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    return await res.json();
  } catch (err) {
    showToast(`Network error: ${err.message}`, "error");
    return { success: false, error: err.message };
  }
}

// ==========================================
// Event Listeners
// ==========================================
function setupEventListeners() {
  // Master Switch
  masterToggle.addEventListener("change", async (e) => {
    const targetState = e.target.checked;
    const res = await apiPost("/api/shield/toggle", { state: targetState });
    if (res.strict_locked) {
      masterToggle.checked = true;
      openPledgeModal();
      return;
    }
    if (!res.success) {
      masterToggle.checked = !targetState;
      showToast(res.error || "Failed to update shield", "error");
    } else {
      showToast(
        targetState ? "Network Shield Activated! Domains null-routed." : "Network Shield Paused.",
        targetState ? "success" : "info"
      );
      fetchStatus();
    }
  });

  // Elevate Button
  btnElevate.addEventListener("click", async () => {
    showToast("Launching elevated UAC prompt...", "info");
    const res = await apiPost("/api/system/elevate");
    if (res.success) {
      showToast("Administrator prompt opened. Please accept Windows UAC.", "success");
    }
  });

  // Timer Presets
  timerPresetsGroup.addEventListener("click", (e) => {
    const chip = e.target.closest(".chip-preset");
    if (!chip || appState.focus_session?.active) return;
    document.querySelectorAll(".chip-preset").forEach((c) => c.classList.remove("active"));
    chip.classList.add("active");
    selectedTimerMinutes = parseInt(chip.dataset.mins, 10);
    renderIdleTimer();
  });

  // Start Focus Session
  btnStartTimer.addEventListener("click", async () => {
    const isStrict = strictModeToggle.checked;
    const res = await apiPost("/api/timer/start", {
      duration_minutes: selectedTimerMinutes,
      strict_mode: isStrict,
    });
    if (res.success) {
      showToast(`Locked in for ${selectedTimerMinutes}m focus session!`, "success");
      fetchStatus();
    } else {
      showToast(res.error || "Could not start session", "error");
    }
  });

  // Stop Focus Session Early
  btnStopTimer.addEventListener("click", () => {
    if (appState.focus_session?.strict_mode) {
      openPledgeModal();
    } else {
      stopSessionDirect();
    }
  });

  // Pledge modal interactions
  pledgeInput.addEventListener("input", (e) => {
    const typed = e.target.value.trim().toLowerCase();
    btnPledgeConfirm.disabled = (typed !== EXPECTED_PLEDGE);
  });

  btnPledgeConfirm.addEventListener("click", async () => {
    const res = await apiPost("/api/timer/stop", {
      force: true,
      pledge: pledgeInput.value.trim().toLowerCase(),
    });
    if (res.success) {
      closePledgeModal();
      showToast("Session unlocked. Stay vigilant against distraction!", "info");
      fetchStatus();
    } else {
      showToast(res.error || "Pledge failed", "error");
    }
  });

  btnPledgeCancel.addEventListener("click", () => {
    closePledgeModal();
    showToast("Proud of you! Keeping the focus shield active.", "success");
  });

  // Live Domain Input Preview
  domainInput.addEventListener("input", (e) => {
    clearTimeout(debounceTimer);
    const val = e.target.value.trim();
    if (!val || val.length < 2) {
      domainPreviewBanner.classList.add("hidden");
      return;
    }
    debounceTimer = setTimeout(async () => {
      try {
        const res = await fetch(`/api/preview-domain?q=${encodeURIComponent(val)}`);
        const data = await res.json();
        if (data.count > 0) {
          previewCount.textContent = `${data.count} hostnames covered`;
          previewHostsChips.innerHTML = data.expanded
            .slice(0, 10)
            .map((h) => `<span class="host-chip">${escapeHtml(h)}</span>`)
            .join("");
          if (data.count > 10) {
            previewHostsChips.innerHTML += `<span class="host-chip">+${data.count - 10} more</span>`;
          }
          domainPreviewBanner.classList.remove("hidden");
        } else {
          domainPreviewBanner.classList.add("hidden");
        }
      } catch (e) {
        domainPreviewBanner.classList.add("hidden");
      }
    }, 200);
  });

  // Add Domain Form Submit
  addForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    const val = domainInput.value.trim();
    if (!val) return;

    const res = await apiPost("/api/domain/add", { domain: val });
    if (res.success) {
      showToast(`Blocked ${res.domain} (+${res.expanded_count} endpoints)!`, "success");
      domainInput.value = "";
      domainPreviewBanner.classList.add("hidden");
      fetchStatus();
    } else {
      showToast(res.error || "Failed to add domain", "error");
    }
  });

  // Blocklist Search Filter
  blocklistSearch.addEventListener("input", (e) => {
    renderDomainsList(e.target.value.trim().toLowerCase());
  });

  // Diagnostics Test
  btnRunDiag.addEventListener("click", runDiagnosticTest);
  diagInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") runDiagnosticTest();
  });

  // DoH Policy Enforce
  btnToggleDoh.addEventListener("click", async () => {
    const isOff = appState.browser_doh?.is_doh_disabled_by_policy;
    const res = await apiPost("/api/system/doh-policy", { disable: !isOff });
    if (res.success) {
      showToast(res.message, "success");
      fetchStatus();
    } else {
      showToast(res.error || "Requires administrator permissions", "error");
    }
  });
}

async function stopSessionDirect() {
  const res = await apiPost("/api/timer/stop", { force: true });
  if (res.success) {
    showToast("Focus session ended.", "info");
    fetchStatus();
  }
}

function openPledgeModal() {
  pledgeInput.value = "";
  btnPledgeConfirm.disabled = true;
  pledgeModal.classList.remove("hidden");
  pledgeInput.focus();
}

function closePledgeModal() {
  pledgeModal.classList.add("hidden");
}

// ==========================================
// Renderers
// ==========================================
function renderAll() {
  renderHeaderStatus();
  renderMasterHero();
  renderTimer();
  renderPresets();
  renderDomainsList(blocklistSearch.value.trim().toLowerCase());
  renderDohStatus();
}

function renderHeaderStatus() {
  const isShieldOn = appState.shield_active;
  masterToggle.checked = isShieldOn;

  if (isShieldOn) {
    osStatusPill.className = "status-pill active";
    osStatusText.textContent = "OS RESOLVER BLOCKED";
  } else {
    osStatusPill.className = "status-pill standby";
    osStatusText.textContent = "SHIELD STANDBY";
  }

  if (appState.is_admin) {
    adminBadge.className = "badge admin-ok";
    adminBadge.innerHTML = `<span class="badge-icon">🛡️</span><span>Administrator (Instant Sync)</span>`;
    btnElevate.classList.add("hidden");
  } else {
    adminBadge.className = "badge admin-warn";
    adminBadge.innerHTML = `<span class="badge-icon">⚠️</span><span>Standard User</span>`;
    btnElevate.classList.remove("hidden");
  }

  if (appState.stats?.last_sync_time) {
    const d = new Date(appState.stats.last_sync_time * 1000);
    syncTimestamp.textContent = `Last network sync: ${d.toLocaleTimeString()}`;
  }
}

function renderMasterHero() {
  const isShieldOn = appState.shield_active;

  if (isShieldOn) {
    masterHeroCard.classList.remove("disabled");
    masterTitle.textContent = "Network Blocker: Active";
    masterSubtitle.textContent = "Direct OS-level host resolution drop (0.0.0.0). All browsers & apps blocked.";
  } else {
    masterHeroCard.classList.add("disabled");
    masterTitle.textContent = "Network Blocker: Standby";
    masterSubtitle.textContent = "Shield is paused. Toggle on to activate OS-level protection.";
  }

  const enabledCustomCount = appState.custom_domains?.filter((d) => d.enabled).length || 0;
  let presetDomainsCount = 0;
  Object.values(appState.presets || {}).forEach((p) => {
    if (p.enabled) presetDomainsCount += (p.domains || []).length;
  });

  metricBlockedDomains.textContent = enabledCustomCount + presetDomainsCount;
  metricActiveHosts.textContent = appState.total_active_hostnames || 0;
}

function renderIdleTimer() {
  const mins = String(selectedTimerMinutes).padStart(2, "0");
  timerReadout.textContent = `${mins}:00`;
  timerSubtext.textContent = "READY TO FOCUS";
  timerRing.style.strokeDashoffset = "0";
}

function renderTimer() {
  const session = appState.focus_session || {};
  const isActive = session.active && session.end_time > appState.current_time;

  if (!isActive) {
    if (timerInterval) {
      clearInterval(timerInterval);
      timerInterval = null;
    }
    timerStatusBadge.className = "badge badge-subtle";
    timerStatusBadge.textContent = "IDLE";
    btnStartTimer.classList.remove("hidden");
    btnStopTimer.classList.add("hidden");
    renderIdleTimer();
    return;
  }

  // Active Session
  timerStatusBadge.className = session.strict_mode ? "badge admin-warn" : "badge admin-ok";
  timerStatusBadge.textContent = session.strict_mode ? "STRICT LOCK" : "FOCUS ACTIVE";
  btnStartTimer.classList.add("hidden");
  btnStopTimer.classList.remove("hidden");

  updateTimerTick();
  if (!timerInterval) {
    timerInterval = setInterval(updateTimerTick, 1000);
  }
}

function updateTimerTick() {
  const session = appState.focus_session || {};
  const now = Math.floor(Date.now() / 1000);
  const remaining = Math.max(0, session.end_time - now);

  if (remaining <= 0) {
    clearInterval(timerInterval);
    timerInterval = null;
    fetchStatus();
    return;
  }

  const m = Math.floor(remaining / 60);
  const s = remaining % 60;
  timerReadout.textContent = `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
  timerSubtext.textContent = session.strict_mode ? "STRICT FOCUS MODE" : "DEEP WORK ACTIVE";

  const total = session.duration_seconds || 1;
  const progressRatio = (total - remaining) / total;
  const offset = CIRCUMFERENCE * (1 - progressRatio);
  timerRing.style.strokeDashoffset = `${offset}`;
}

function renderPresets() {
  presetsGrid.innerHTML = "";
  const presets = appState.presets || {};

  const iconMap = {
    tv: "🎬",
    share: "📱",
    compass: "🌀",
    gamepad: "🎮",
    shield: "🔞",
    newspaper: "📰",
  };

  for (const [id, preset] of Object.entries(presets)) {
    const card = document.createElement("div");
    card.className = `preset-card ${preset.enabled ? "active" : ""}`;
    card.innerHTML = `
      <div class="preset-info">
        <div class="preset-icon">${iconMap[preset.icon] || "🛡️"}</div>
        <div>
          <span class="preset-name">${escapeHtml(preset.name)}</span>
          <span class="preset-count">${(preset.domains || []).length} major services</span>
        </div>
      </div>
      <label class="toggle-sm">
        <input type="checkbox" ${preset.enabled ? "checked" : ""} data-preset="${id}">
        <span class="toggle-sm-slider"></span>
      </label>
    `;

    const chk = card.querySelector("input");
    chk.addEventListener("change", async (e) => {
      const isChecked = e.target.checked;
      const res = await apiPost("/api/preset/toggle", { preset_id: id, enabled: isChecked });
      if (res.success) {
        showToast(`${preset.name} ${isChecked ? "shield enabled" : "disabled"}.`, "info");
        fetchStatus();
      } else {
        chk.checked = !isChecked;
        showToast(res.error || "Failed to update preset", "error");
      }
    });

    presetsGrid.appendChild(card);
  }
}

function renderDomainsList(filterQuery = "") {
  domainsList.innerHTML = "";
  const list = appState.custom_domains || [];
  const filtered = list.filter((item) => item.domain.includes(filterQuery));

  if (filtered.length === 0) {
    domainsList.innerHTML = `
      <div class="empty-state">
        ${filterQuery ? "No matching websites found." : "No custom websites added yet. Type a domain above to block it."}
      </div>
    `;
    return;
  }

  filtered.forEach((item) => {
    const row = document.createElement("div");
    row.className = "domain-row";
    row.innerHTML = `
      <div class="domain-main-col">
        <div class="domain-favicon">${item.domain.charAt(0).toUpperCase()}</div>
        <div>
          <span class="domain-name">${escapeHtml(item.domain)}</span>
        </div>
      </div>
      <div class="domain-actions-col">
        <label class="toggle-sm">
          <input type="checkbox" ${item.enabled ? "checked" : ""} data-domain="${escapeHtml(item.domain)}">
          <span class="toggle-sm-slider"></span>
        </label>
        <button class="btn-icon-del" title="Remove ${escapeHtml(item.domain)}">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="3 6 5 6 21 6"/>
            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
          </svg>
        </button>
      </div>
    `;

    // Toggle domain
    const toggleInput = row.querySelector("input");
    toggleInput.addEventListener("change", async (e) => {
      const isChecked = e.target.checked;
      const res = await apiPost("/api/domain/toggle", { domain: item.domain, enabled: isChecked });
      if (res.success) {
        showToast(`${item.domain} ${isChecked ? "blocked" : "unblocked"}.`, "info");
        fetchStatus();
      } else {
        toggleInput.checked = !isChecked;
        showToast(res.error || "Toggle failed", "error");
      }
    });

    // Delete domain
    const delBtn = row.querySelector(".btn-icon-del");
    delBtn.addEventListener("click", async () => {
      const res = await apiPost("/api/domain/remove", { domain: item.domain });
      if (res.success) {
        showToast(`Removed ${item.domain} from blocklist.`, "info");
        fetchStatus();
      } else {
        showToast(res.error || "Failed to remove domain", "error");
      }
    });

    domainsList.appendChild(row);
  });
}

function renderDohStatus() {
  const doh = appState.browser_doh || {};
  if (doh.is_doh_disabled_by_policy) {
    btnToggleDoh.className = "btn btn-sm btn-primary";
    dohBtnText.textContent = "✓ Browser Shield Enforced";
  } else {
    btnToggleDoh.className = "btn btn-sm btn-outline";
    dohBtnText.textContent = "Enforce Browser Shield";
  }
}

// ==========================================
// Diagnostics Sandbox
// ==========================================
async function runDiagnosticTest() {
  const target = diagInput.value.trim();
  if (!target) return;

  btnRunDiag.disabled = true;
  btnRunDiag.innerHTML = "<span>Probing...</span>";
  diagResultBox.className = "diag-result-box";
  diagResultBox.innerHTML = `<span class="text-muted">Probing OS socket resolution and TCP connection for <strong>${escapeHtml(target)}</strong>...</span>`;

  try {
    const res = await apiPost("/api/diagnostics/test", { domain: target });
    btnRunDiag.disabled = false;
    btnRunDiag.innerHTML = "<span>Test Socket</span>";

    const isBlocked = res.is_blocked;
    const badgeClass = isBlocked ? "blocked" : "open";
    const badgeText = isBlocked ? "BLOCKED (0.0.0.0 / Null Route)" : "PUBLIC / REACHABLE";

    diagResultBox.innerHTML = `
      <div class="diag-status-row">
        <strong>${escapeHtml(res.domain)}</strong>
        <span class="diag-badge ${badgeClass}">${badgeText}</span>
      </div>
      <div class="diag-ips">Resolved IPs: ${escapeHtml((res.resolved_ips || []).join(", ") || "None")}</div>
      <div class="diag-details text-muted" style="margin-top: 4px;">${escapeHtml(res.details)}</div>
      ${isBlocked ? `<div class="text-emerald" style="margin-top: 4px; font-weight: 600;">✓ Embedded iframes and apps attempting to reach this host will fail immediately.</div>` : ""}
    `;
  } catch (err) {
    btnRunDiag.disabled = false;
    btnRunDiag.innerHTML = "<span>Test Socket</span>";
    diagResultBox.innerHTML = `<span class="text-accent-crimson">Test failed: ${escapeHtml(err.message)}</span>`;
  }
}

// ==========================================
// Toast Notifications
// ==========================================
function showToast(message, type = "info") {
  const container = document.getElementById("toast-container");
  const toast = document.createElement("div");
  toast.className = `toast ${type}`;

  const iconMap = {
    success: "✓",
    error: "✕",
    info: "ℹ",
  };

  toast.innerHTML = `<span>${iconMap[type] || "•"}</span><span>${escapeHtml(message)}</span>`;
  container.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = "0";
    toast.style.transform = "translateY(10px)";
    toast.style.transition = "all 0.3s ease";
    setTimeout(() => toast.remove(), 300);
  }, 3800);
}

// Utility: HTML escape
function escapeHtml(str) {
  if (!str) return "";
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}
