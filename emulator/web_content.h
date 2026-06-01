#pragma once

static const char WEB_HTML[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AlkTrack Monitor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;background:#0a1628;color:#e0e8f0;min-height:100vh;padding:24px}
.container{max-width:760px;margin:0 auto}
h1{font-size:1.1rem;color:#64748b;font-weight:500;margin-bottom:24px;letter-spacing:0.5px}
.result-card{background:#111d33;border:1px solid #1e3a5f;border-radius:12px;padding:32px;text-align:center;margin-bottom:20px}
.result-value{font-size:4rem;font-weight:700;color:#22d3ee;line-height:1}
.result-unit{font-size:1.2rem;color:#64748b;margin-top:4px}
.phase{display:inline-block;padding:4px 12px;border-radius:20px;font-size:0.8rem;font-weight:600;text-transform:uppercase;letter-spacing:0.5px;margin-top:12px}
.phase-sleeping{background:#1e293b;color:#94a3b8}
.phase-ref_aerate,.phase-tank_aerate{background:#0c4a6e;color:#38bdf8;animation:pulse 2s infinite}
.phase-startup{background:#422006;color:#fb923c}
.phase-idle{background:#14532d;color:#4ade80}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.6}}
.mode-badge{display:inline-block;padding:3px 10px;border-radius:12px;font-size:0.75rem;font-weight:700;letter-spacing:0.5px;margin-left:8px;vertical-align:middle}
.mode-phdi{background:#0c4a6e;color:#38bdf8}
.mode-titr{background:#3b1a6e;color:#c084fc}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px}
.stat{background:#111d33;border:1px solid #1e3a5f;border-radius:8px;padding:16px}
.stat-label{font-size:0.75rem;color:#64748b;text-transform:uppercase;letter-spacing:0.5px}
.stat-value{font-size:1.4rem;font-weight:600;color:#e2e8f0;margin-top:4px}
.chart-card{background:#111d33;border:1px solid #1e3a5f;border-radius:8px;padding:16px;margin-bottom:20px}
.chart-title{font-size:0.75rem;color:#64748b;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:12px}
svg{width:100%;height:80px;display:block}
.card{background:#111d33;border:1px solid #1e3a5f;border-radius:8px;padding:16px;margin-bottom:12px}
.card-title{font-size:0.8rem;color:#64748b;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:12px;font-weight:600}
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
button{background:#1e3a5f;border:1px solid #2d5a8e;color:#e0e8f0;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:0.85rem;font-weight:500;transition:background 0.15s}
button:hover{background:#2d5a8e}
button.danger{background:#7f1d1d;border-color:#991b1b}
button.danger:hover{background:#991b1b}
button.active{background:#0c4a6e;border-color:#0ea5e9;color:#38bdf8}
button.active-titr{background:#3b1a6e;border-color:#7c3aed;color:#c084fc}
.field-row{display:flex;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}
.field-row label{font-size:0.8rem;color:#94a3b8;width:160px;flex-shrink:0}
input[type=number],input[type=text]{background:#0a1628;border:1px solid #1e3a5f;color:#e0e8f0;padding:7px 10px;border-radius:6px;width:120px;font-size:0.85rem}
input:focus{outline:none;border-color:#22d3ee}
.apply-btn{background:#14532d;border:1px solid #166534;color:#4ade80;padding:6px 14px;border-radius:6px;cursor:pointer;font-size:0.8rem;font-weight:600}
.apply-btn:hover{background:#166534}
/* pH calibration wizard */
.wiz{background:#0a1e38;border:1px solid #1e3a5f;border-radius:8px;padding:16px;margin-top:8px}
.wiz-step{display:none}
.wiz-step.active{display:block}
.wiz-title{font-size:0.9rem;font-weight:600;color:#22d3ee;margin-bottom:8px}
.wiz-desc{font-size:0.8rem;color:#94a3b8;margin-bottom:12px;line-height:1.5}
.wiz-live{font-size:1.6rem;font-weight:700;color:#e2e8f0;margin-bottom:12px}
.wiz-btn-row{display:flex;gap:8px}
.wiz-next{background:#14532d;border:1px solid #166534;color:#4ade80;padding:8px 20px;border-radius:6px;cursor:pointer;font-size:0.85rem;font-weight:600}
.wiz-next:hover{background:#166534}
.wiz-cancel{background:#1e293b;border:1px solid #334155;color:#94a3b8;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:0.85rem}
.meta{font-size:0.7rem;color:#475569;text-align:center;margin-top:16px}
</style>
</head>
<body>
<div class="container">
<h1>AlkTrack Alkalinity Monitor <span id="modebadge" class="mode-badge mode-phdi">pH-DIFF</span></h1>

<!-- Result card -->
<div class="result-card">
  <div class="result-value" id="result">--</div>
  <div class="result-unit">dKH</div>
  <span class="phase phase-startup" id="phase">startup</span>
</div>

<!-- Sensor stats -->
<div class="grid">
  <div class="stat"><div class="stat-label">Ref pH</div><div class="stat-value" id="refph">--</div></div>
  <div class="stat"><div class="stat-label">Tank pH</div><div class="stat-value" id="tankph">--</div></div>
  <div class="stat"><div class="stat-label">Temperature</div><div class="stat-value" id="temp">--</div></div>
  <div class="stat"><div class="stat-label" id="anchlab">pH-Diff Anchor</div><div class="stat-value" id="anchor">--</div></div>
</div>

<!-- Trend chart -->
<div class="chart-card">
  <div class="chart-title">Trend (last 24 readings)</div>
  <svg id="chart" viewBox="0 0 680 80" preserveAspectRatio="none"></svg>
</div>

<!-- Controls -->
<div class="card">
  <div class="card-title">Controls</div>
  <div class="btn-row">
    <button onclick="sendCmd('RUN')">Run Cycle</button>
    <button onclick="sendCmd('STATUS')">Status</button>
    <button onclick="sendCmd('PRIME')">Prime</button>
    <button class="danger" onclick="sendCmd('PUMPS_OFF')">PUMPS OFF</button>
  </div>
</div>

<!-- Mode selection -->
<div class="card">
  <div class="card-title">Operating Mode</div>
  <div class="btn-row">
    <button id="btn-phdi" class="active" onclick="setMode('PHDI')">pH Differential</button>
    <button id="btn-titr" onclick="setMode('TITR')">Acid Titration</button>
  </div>
</div>

<!-- pH Differential settings -->
<div class="card" id="phdi-panel">
  <div class="card-title">pH Differential — Anchor</div>
  <div class="field-row">
    <label>Reference alkalinity</label>
    <input type="number" id="anchorval" step="0.1" min="3" max="20" placeholder="dKH">
    <button class="apply-btn" onclick="applyAnchor()">Apply</button>
  </div>
  <!-- pH probe 3-point calibration wizard -->
  <div style="margin-top:12px">
    <div class="card-title" style="margin-bottom:8px">pH Probe Calibration</div>
    <div class="btn-row">
      <button id="wiz-start-btn" onclick="wizStart()">Start 3-Point Cal (4.01 / 7.00 / 10.01)</button>
    </div>
    <div class="wiz" id="wizard">
      <div class="wiz-step active" id="wiz-0">
        <div class="wiz-title">Ready to calibrate</div>
        <div class="wiz-desc">This wizard walks you through a 3-point pH calibration using standard buffers at pH 7.00, 4.01, and 10.01.<br><br>Have your three buffer solutions ready. Rinse the probe with DI water between each buffer.</div>
        <div class="wiz-btn-row">
          <button class="wiz-next" onclick="wizNext(1)">Begin</button>
          <button class="wiz-cancel" onclick="wizCancel()">Cancel</button>
        </div>
      </div>
      <div class="wiz-step" id="wiz-1">
        <div class="wiz-title">Step 1 of 3 — pH 7.00 buffer (mid-point)</div>
        <div class="wiz-desc">Place probe in pH 7.00 buffer solution. Wait for reading to stabilise, then confirm.</div>
        <div class="wiz-live" id="wiz-live-1">-- pH</div>
        <div class="wiz-btn-row">
          <button class="wiz-next" onclick="wizConfirm(1,'CAL PH MID')">Confirm pH 7.00</button>
          <button class="wiz-cancel" onclick="wizCancel()">Cancel</button>
        </div>
      </div>
      <div class="wiz-step" id="wiz-2">
        <div class="wiz-title">Step 2 of 3 — pH 4.01 buffer (low-point)</div>
        <div class="wiz-desc">Rinse probe, then place in pH 4.01 buffer. Wait for reading to stabilise, then confirm.</div>
        <div class="wiz-live" id="wiz-live-2">-- pH</div>
        <div class="wiz-btn-row">
          <button class="wiz-next" onclick="wizConfirm(2,'CAL PH LOW')">Confirm pH 4.01</button>
          <button class="wiz-cancel" onclick="wizCancel()">Cancel</button>
        </div>
      </div>
      <div class="wiz-step" id="wiz-3">
        <div class="wiz-title">Step 3 of 3 — pH 10.01 buffer (high-point)</div>
        <div class="wiz-desc">Rinse probe, then place in pH 10.01 buffer. Wait for reading to stabilise, then confirm.</div>
        <div class="wiz-live" id="wiz-live-3">-- pH</div>
        <div class="wiz-btn-row">
          <button class="wiz-next" onclick="wizConfirm(3,'CAL PH HIGH')">Confirm pH 10.01</button>
          <button class="wiz-cancel" onclick="wizCancel()">Cancel</button>
        </div>
      </div>
      <div class="wiz-step" id="wiz-4">
        <div class="wiz-title">Calibration complete</div>
        <div class="wiz-desc">All three calibration points have been sent to the EZO-pH circuit. The probe will use the new calibration immediately.</div>
        <div class="wiz-btn-row">
          <button class="wiz-cancel" onclick="wizCancel()">Close</button>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- Acid titration settings (hidden until TITR mode) -->
<div class="card" id="titr-panel" style="display:none">
  <div class="card-title">Acid Titration — Calibration</div>
  <div class="field-row">
    <label>Acid normality (N)</label>
    <input type="number" id="acidnorm" step="0.001" min="0.001" max="2" placeholder="0.1000">
    <button class="apply-btn" onclick="applyField('CAL ACID ','acidnorm')">Apply</button>
  </div>
  <div class="field-row">
    <label>Sample volume (mL)</label>
    <input type="number" id="sampleml" step="0.5" min="5" max="200" placeholder="25.0">
    <button class="apply-btn" onclick="applyField('CAL SAMPLEML ','sampleml')">Apply</button>
  </div>
  <div class="field-row">
    <label>Dose per step (mL)</label>
    <input type="number" id="doseml" step="0.001" min="0.001" max="0.5" placeholder="0.020">
    <button class="apply-btn" onclick="applyField('CAL DOSE ','doseml')">Apply</button>
  </div>
  <div class="field-row">
    <label>Sample pump steps/mL</label>
    <input type="number" id="samplespm" step="100" min="100" max="50000" placeholder="3200">
    <button class="apply-btn" onclick="applyField('CAL SAMPLESPM ','samplespm')">Apply</button>
  </div>
  <div class="field-row">
    <label>Acid pump steps/mL</label>
    <input type="number" id="acidspm" step="100" min="100" max="50000" placeholder="6400">
    <button class="apply-btn" onclick="applyField('CAL ACIDSPM ','acidspm')">Apply</button>
  </div>
</div>

<div class="meta" id="metamsg">Auto-refreshes every 2s | Simulation mode</div>
</div>

<script>
var g_mode = 0; // 0=PHDI, 1=TITR
var g_wizStep = 0;
var g_wizActive = false;
var g_liveRef = '--', g_liveTank = '--';

function update() {
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('result').textContent = d.lastResult > 0 ? d.lastResult.toFixed(2) : '--';
    g_liveRef  = d.refPH  > 0 ? d.refPH.toFixed(4)  : '--';
    g_liveTank = d.tankPH > 0 ? d.tankPH.toFixed(4) : '--';
    document.getElementById('refph').textContent  = g_liveRef;
    document.getElementById('tankph').textContent = g_liveTank;
    document.getElementById('temp').textContent   = d.temp > 0 ? d.temp.toFixed(1) + ' C' : '--';

    if (d.mode !== undefined && d.mode !== g_mode) {
      g_mode = d.mode;
      applyModeUI(g_mode);
    }

    var livePhReading = g_mode === 0 ? g_liveRef : g_liveTank;
    if (g_wizActive) {
      var el = document.getElementById('wiz-live-' + g_wizStep);
      if (el) el.textContent = (livePhReading !== '--' ? livePhReading : '--') + ' pH';
    }

    var anchEl = document.getElementById('anchor');
    if (g_mode === 0) {
      anchEl.textContent = d.anchor > 0 ? d.anchor.toFixed(2) + ' dKH' : '--';
    } else {
      anchEl.textContent = d.acidNorm !== undefined ? d.acidNorm.toFixed(4) + ' N' : '--';
    }

    // Populate titration fields if not focused
    fillIfBlur('acidnorm',  d.acidNorm,   4);
    fillIfBlur('sampleml',  d.sampleML,   1);
    fillIfBlur('doseml',    d.doseMl,     4);
    fillIfBlur('samplespm', d.sampleSPM,  0);
    fillIfBlur('acidspm',   d.acidSPM,    0);
    fillIfBlur('anchorval', d.anchor,     2);

    var el = document.getElementById('phase');
    el.textContent = d.phase.replace(/_/g,' ');
    el.className = 'phase phase-' + d.phase;
    drawChart(d.history);
  }).catch(function(){});
}

function fillIfBlur(id, val, prec) {
  var el = document.getElementById(id);
  if (!el || document.activeElement === el) return;
  if (val !== undefined && val !== null && val > 0) el.value = val.toFixed ? val.toFixed(prec) : val;
}

function applyModeUI(mode) {
  var isPhdi = (mode === 0);
  document.getElementById('phdi-panel').style.display = isPhdi ? '' : 'none';
  document.getElementById('titr-panel').style.display = isPhdi ? 'none' : '';
  var bPhdi = document.getElementById('btn-phdi');
  var bTitr = document.getElementById('btn-titr');
  bPhdi.className = isPhdi ? 'active' : '';
  bTitr.className = isPhdi ? '' : 'active-titr active';
  var badge = document.getElementById('modebadge');
  badge.textContent = isPhdi ? 'pH-DIFF' : 'TITR';
  badge.className = 'mode-badge ' + (isPhdi ? 'mode-phdi' : 'mode-titr');
  var anchlab = document.getElementById('anchlab');
  anchlab.textContent = isPhdi ? 'pH-Diff Anchor' : 'Acid Normality';
}

function setMode(m) {
  sendCmd('MODE ' + m);
  setTimeout(update, 600);
}

function drawChart(data) {
  var svg = document.getElementById('chart');
  if (!data || data.length < 2) {
    svg.innerHTML = '<text x="340" y="45" text-anchor="middle" fill="#475569" font-size="12">Waiting for data...</text>';
    return;
  }
  var min = Math.min.apply(null,data)-0.5, max = Math.max.apply(null,data)+0.5;
  if (max-min < 1) { min -= 0.5; max += 0.5; }
  var w=680, h=80, pts=[];
  for (var i=0;i<data.length;i++) {
    var x = (i/(data.length-1))*w;
    var y = h - ((data[i]-min)/(max-min))*h;
    pts.push(x.toFixed(1)+','+y.toFixed(1));
  }
  var s = '<polyline points="'+pts.join(' ')+'" fill="none" stroke="#22d3ee" stroke-width="2" stroke-linejoin="round"/>';
  for (var i=0;i<data.length;i++) {
    var x = (i/(data.length-1))*w;
    var y = h - ((data[i]-min)/(max-min))*h;
    s += '<circle cx="'+x.toFixed(1)+'" cy="'+y.toFixed(1)+'" r="3" fill="#22d3ee"/>';
  }
  svg.innerHTML = s;
}

function sendCmd(cmd) {
  fetch('/api/command', {method:'POST', body:cmd}).then(function(){setTimeout(update,500);});
}

function applyAnchor() {
  var v = document.getElementById('anchorval').value;
  if (v) sendCmd('CAL ' + v);
}

function applyField(prefix, id) {
  var v = document.getElementById(id).value;
  if (v) sendCmd(prefix + v);
}

// ── pH calibration wizard ────────────────────────────────────

function wizStart() {
  g_wizActive = true;
  g_wizStep = 0;
  showWizStep(0);
  document.getElementById('wizard').style.display = '';
  document.getElementById('wiz-start-btn').style.display = 'none';
}

function showWizStep(n) {
  for (var i = 0; i <= 4; i++) {
    var el = document.getElementById('wiz-' + i);
    if (el) el.className = 'wiz-step' + (i === n ? ' active' : '');
  }
  g_wizStep = n;
}

function wizNext(step) {
  showWizStep(step);
}

function wizConfirm(step, cmd) {
  sendCmd(cmd);
  if (step < 3) {
    showWizStep(step + 1);
  } else {
    showWizStep(4);
    g_wizActive = false;
  }
}

function wizCancel() {
  g_wizActive = false;
  showWizStep(0);
  document.getElementById('wizard').style.display = 'none';
  document.getElementById('wiz-start-btn').style.display = '';
}

// hide wizard by default
document.getElementById('wizard').style.display = 'none';

setInterval(update, 2000);
update();
</script>
</body>
</html>
)rawhtml";
