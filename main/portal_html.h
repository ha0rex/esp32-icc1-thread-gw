#pragma once

/* Same visual system as the Zigbee gateway portal, with Thread / Matter pages. */
static const char PORTAL_HTML[] = R"HTML(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ICC Thread Gateway</title>
<style>
:root{--bg:#0a101c;--panel:#121a2b;--panel2:#161f32;--line:#243049;--text:#e8eef8;--muted:#8b9bb4;
--accent:#1ec8a5;--accent2:#3d8bfd;--danger:#ff6b6b;--ok:#1ec8a5;--warn:#f0b429;
--radius:12px;--shadow:0 8px 28px rgba(0,0,0,.28)}
*{box-sizing:border-box}
body{margin:0;font-family:'Segoe UI',ui-sans-serif,system-ui,-apple-system,sans-serif;
background:radial-gradient(900px 480px at 8% -8%,#16324a 0%,transparent 55%),
radial-gradient(700px 420px at 100% 0%,#12263a 0%,transparent 48%),var(--bg);
color:var(--text);min-height:100vh}
header.app{position:sticky;top:0;z-index:30;backdrop-filter:blur(14px);
background:rgba(10,16,28,.88);border-bottom:1px solid var(--line)}
.wrap{max-width:920px;margin:0 auto;padding-left:1.25rem;padding-right:1.25rem}
.brand{display:flex;align-items:center;justify-content:space-between;gap:1rem;padding:.85rem 0 .35rem}
.logo{display:flex;align-items:center;gap:.7rem}
.mark{width:34px;height:34px;border-radius:9px;background:linear-gradient(135deg,var(--accent),#0e8f9a);
box-shadow:0 0 0 1px rgba(30,200,165,.35),0 6px 18px rgba(30,200,165,.22);flex:0 0 auto}
.logo h1{font-size:1.02rem;margin:0;font-weight:650;letter-spacing:.01em}
.logo p{margin:.1rem 0 0;color:var(--muted);font-size:.74rem}
.pill{font-size:.7rem;padding:.26rem .6rem;border-radius:999px;border:1px solid var(--line);
background:rgba(255,255,255,.03);color:var(--muted);white-space:nowrap}
.pill.ok{color:var(--ok);border-color:rgba(30,200,165,.35);background:rgba(30,200,165,.08)}
.pill.bad{color:var(--danger);border-color:rgba(255,107,107,.35);background:rgba(255,107,107,.08)}
.pill.warn{color:var(--warn);border-color:rgba(240,180,41,.35);background:rgba(240,180,41,.08)}
nav.tabs{display:flex;gap:.25rem;overflow:auto;padding:.15rem 0 1.1rem}
nav.tabs button{border:0;background:transparent;color:var(--muted);padding:.5rem .8rem;
border-radius:999px;font-weight:600;font-size:.84rem;cursor:pointer;white-space:nowrap}
nav.tabs button:hover{color:var(--text)}
nav.tabs button.active{background:rgba(30,200,165,.14);color:var(--accent)}
main.wrap{margin-top:.675rem;padding-top:.5rem;padding-bottom:2.25rem}
.grid{display:grid;gap:.9rem}
.cards{display:grid;gap:.9rem;grid-template-columns:repeat(2,minmax(0,1fr))}
@media(min-width:720px){.cards{grid-template-columns:repeat(4,minmax(0,1fr))}}
.card{background:linear-gradient(180deg,rgba(255,255,255,.028),transparent),var(--panel);
border:1px solid var(--line);border-radius:var(--radius);padding:1rem 1.05rem;box-shadow:var(--shadow)}
.card h2{margin:0 0 .7rem;font-size:.68rem;letter-spacing:.09em;text-transform:uppercase;
color:var(--muted);font-weight:700}
.metric{font-size:1.35rem;font-weight:700;letter-spacing:-.02em;margin:0 0 .2rem;line-height:1.15}
dl.kv{display:grid;grid-template-columns:minmax(7.5rem,9.5rem) 1fr;gap:.4rem .9rem;margin:0}
dl.kv dt{color:var(--muted);font-size:.8rem;padding:.12rem 0}
dl.kv dd{margin:0;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.82rem;
word-break:break-all;padding:.12rem 0;color:var(--text)}
label{display:block;color:var(--muted);font-size:.76rem;margin:.65rem 0 .25rem;font-weight:600}
input,select{width:100%;padding:.68rem .75rem;border-radius:9px;border:1px solid var(--line);
background:#0d1524;color:var(--text);font-size:.92rem}
input:focus,select:focus{outline:2px solid rgba(30,200,165,.35);border-color:rgba(30,200,165,.5)}
.row{display:flex;gap:.55rem;align-items:stretch;flex-wrap:wrap}.row>*{flex:1 1 140px}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:.45rem;
width:100%;padding:.68rem .9rem;border-radius:9px;border:0;cursor:pointer;
font:inherit;font-weight:650;font-size:.88rem;line-height:1.2;
background:linear-gradient(135deg,var(--accent),#149e90);color:#041512;margin-top:.75rem}
.btn:hover:not(:disabled){transform:translateY(-1px);box-shadow:0 6px 16px rgba(30,200,165,.22)}
.btn.secondary{background:var(--panel2);color:var(--text);border:1px solid var(--line)}
.btn.danger{background:linear-gradient(135deg,#ff6b6b,#d94848);color:#fff}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn.sm{width:auto;margin:0;padding:.42rem .7rem;font-size:.78rem}
.hint{color:var(--muted);font-size:.78rem;margin:.5rem 0 0;line-height:1.45}
.banner{padding:.8rem .95rem;border-radius:10px;border:1px solid var(--line);background:rgba(61,139,253,.08);
color:#cfe0ff;margin-bottom:.9rem;font-size:.88rem;line-height:1.4}
.banner.ok{background:rgba(30,200,165,.08);border-color:rgba(30,200,165,.3);color:#baf7e8}
.banner.warn{background:rgba(240,180,41,.08);border-color:rgba(240,180,41,.3);color:#ffe6a8}
.empty{text-align:center;padding:2rem 1rem;color:var(--muted);border:1px dashed var(--line);
border-radius:10px;background:rgba(255,255,255,.015)}
.empty strong{display:block;color:var(--text);font-size:1rem;margin-bottom:.3rem}
.device{display:flex;justify-content:space-between;gap:1rem;align-items:center;padding:.85rem .55rem;
border-bottom:1px solid var(--line);cursor:pointer;border-radius:8px;transition:background .12s}
.device:hover{background:rgba(255,255,255,.03)}
.device:last-child{border-bottom:0}
.device .id{font-family:ui-monospace,Menlo,monospace;font-size:.8rem}
.device .title{font-weight:650;font-size:.92rem;margin-bottom:.15rem;display:flex;align-items:center;gap:.45rem;width:100%}
.device .title-text{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wifi-wrap{margin-left:auto;display:inline-flex;align-items:center;gap:.35rem;color:var(--muted);font-size:.7rem;
font-family:ui-monospace,Menlo,monospace;font-weight:550;flex:0 0 auto}
.wifi{position:relative;width:16px;height:12px;flex:0 0 auto;overflow:hidden}
.wifi i,.wifi b{position:absolute;left:50%;transform:translateX(-50%);box-sizing:border-box}
.wifi i{border:1.6px solid transparent;border-top-color:rgba(139,155,180,.32);border-radius:50%}
.wifi i:nth-child(1){width:16px;height:16px;top:-1px}
.wifi i:nth-child(2){width:11px;height:11px;top:2px}
.wifi i:nth-child(3){width:6px;height:6px;top:5px}
.wifi b{width:2.5px;height:2.5px;border-radius:50%;background:rgba(139,155,180,.32);bottom:0}
.wifi[data-lvl="4"] i{border-top-color:var(--accent)}
.wifi[data-lvl="4"] b{background:var(--accent)}
.wifi[data-lvl="3"] i:nth-child(n+2){border-top-color:var(--accent)}
.wifi[data-lvl="3"] b{background:var(--accent)}
.wifi[data-lvl="2"] i:nth-child(3){border-top-color:var(--accent)}
.wifi[data-lvl="2"] b{background:var(--accent)}
.wifi[data-lvl="1"] b{background:var(--warn)}
.device .meta{color:var(--muted);font-size:.76rem;margin-top:.12rem}
.device .kind{color:var(--text);font-size:.78rem;margin-top:.2rem;font-weight:550}
.btn-row{display:grid;grid-template-columns:1fr 9.5rem;gap:.4rem;margin:.35rem 0}
.device .vals{display:flex;flex-wrap:wrap;gap:.35rem;margin-top:.4rem}
.device .btn{width:auto;margin:0;flex:0 0 auto;align-self:center}
.chip{font-size:.72rem;padding:.18rem .45rem;border-radius:6px;background:rgba(30,200,165,.1);
border:1px solid rgba(30,200,165,.22);color:#baf7e8}
.ap-list{display:flex;flex-direction:column;gap:.45rem;margin:.55rem 0 0;max-height:min(42vh,22rem);overflow:auto}
.ap-row{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:.55rem;
padding:.65rem .75rem;border:1px solid var(--line);border-radius:10px;background:rgba(0,0,0,.18)}
.ap-ssid{font-weight:650;font-size:.9rem}
.ap-detail{color:var(--muted);font-size:.72rem;font-family:ui-monospace,Menlo,Consolas,monospace;margin-top:.2rem}
.hidden{display:none!important}
#toasts{position:fixed;right:1rem;bottom:1rem;z-index:50;display:flex;flex-direction:column;gap:.5rem;
max-width:min(360px,calc(100vw - 2rem));pointer-events:none}
.toast{padding:.75rem .9rem;border-radius:10px;border:1px solid var(--line);
background:rgba(18,26,43,.96);box-shadow:var(--shadow);font-size:.86rem}
.toast.ok{border-color:rgba(30,200,165,.4)}.toast.err{border-color:rgba(255,107,107,.45)}
.qrbox{background:#fff;border-radius:16px;padding:16px;width:max-content;margin:.2rem auto .9rem;
box-shadow:0 0 0 1px rgba(255,255,255,.08)}
.hk-setup{text-align:center}
.hk-setup .code{font-size:1.7rem;font-weight:750;letter-spacing:.14em;margin:.15rem 0 .35rem}
.bootlog{margin:.7rem 0 0;padding:.7rem .75rem;max-height:16rem;overflow:auto;white-space:pre-wrap;
background:#0d1524;border:1px solid var(--line);border-radius:10px;color:#c5d2e6;
font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.72rem;line-height:1.45}
.modal-backdrop{position:fixed;inset:0;z-index:40;background:rgba(4,8,16,.62);backdrop-filter:blur(4px);
display:flex;align-items:flex-end;justify-content:center;padding:1rem}
@media(min-width:720px){.modal-backdrop{align-items:center;padding:1.25rem}}
.modal{width:min(920px,100%);max-height:min(92vh,760px);overflow:hidden;display:flex;flex-direction:column;
background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:var(--shadow);
padding:1.1rem 1.2rem 1.15rem}
.modal-head{flex:0 0 auto;margin:0 0 .85rem}
.modal h3{margin:0 0 .3rem;font-size:1.05rem}
.modal .sub{color:var(--muted);font-size:.78rem;margin:0;font-family:ui-monospace,Menlo,monospace}
.modal-body{flex:1 1 auto;min-height:0;display:grid;gap:1rem 1.35rem;grid-template-columns:1fr}
@media(min-width:720px){.modal-body{grid-template-columns:1fr 1fr;align-items:stretch}}
.modal-meta{min-height:0;overflow:auto;padding-right:.15rem}
.modal-meta dl.kv{margin:0;grid-template-columns:minmax(6.8rem,8.2rem) 1fr;gap:.32rem .7rem}
.modal-meta dl.kv dt,.modal-meta dl.kv dd{padding:.06rem 0;font-size:.78rem}
.modal-edit{min-height:0;overflow:auto;display:flex;flex-direction:column}
.modal-edit>label:first-child{margin-top:0}
.modal-foot{flex:0 0 auto;margin-top:.85rem}
.modal-actions{display:flex;flex-wrap:wrap;gap:.5rem}
.modal-actions .btn{margin-top:0;flex:1 1 auto;width:auto}
.modal-foot>.hint{margin:.65rem 0 0}
.sniff-log{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.72rem;
line-height:1.45;background:#0b1220;border:1px solid var(--line);border-radius:10px;
padding:.65rem .75rem;max-height:min(70vh,560px);overflow:auto;white-space:pre-wrap;word-break:break-word;
color:#c8d4e8}
.sniff-log .rx{color:#7dffa8}.sniff-log .tx{color:#7ec8ff}.sniff-log .evt{color:#ffd27d}
.sniff-log .meta{color:#7a869c}
.sniff-toolbar{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;margin-bottom:.75rem}
.sniff-toolbar label.check{margin:0;display:flex;align-items:center;gap:.35rem;color:var(--muted);font-size:.78rem}
.sniff-toolbar label.check input{width:auto;margin:0}
.pill.on{background:rgba(30,200,165,.18);color:var(--accent)}
footer{color:#66748c;text-align:center;font-size:.7rem;padding:0 1rem 1.75rem}
@media(max-width:560px){.cards{grid-template-columns:1fr 1fr}}
@media(max-width:420px){.cards{grid-template-columns:1fr}}
</style></head><body>
<header class="app"><div class="wrap">
<div class="brand"><div class="logo"><div class="mark"></div><div>
<h1>ICC Thread Gateway</h1><p>Thread · Matter controller</p></div></div>
<div id="topPill" class="pill">…</div></div>
<nav class="tabs">
<button type="button" data-tab="overview" class="active">Overview</button>
<button type="button" data-tab="thread">Thread</button>
<button type="button" data-tab="devices">Devices</button>
<button type="button" data-tab="homekit">HomeKit</button>
<button type="button" data-tab="sniff">Sniffer</button>
<button type="button" data-tab="system">System</button>
</nav></div></header>
<main class="wrap">
<section id="view-overview" class="grid">
<div class="cards">
<div class="card"><h2>Thread radio</h2><div class="metric" id="mIcc">—</div>
<p class="hint" id="mIccSub">ICC RCP / Spinel</p></div>
<div class="card"><h2>Network</h2><div class="metric" id="mNet">—</div>
<p class="hint" id="mNetSub">channel · PAN</p></div>
<div class="card"><h2>Devices</h2><div class="metric" id="mDev">0</div>
<p class="hint" id="mDevSub">paired on this fabric</p></div>
<div class="card"><h2>Wi‑Fi</h2><div class="metric" id="mWifi">—</div>
<p class="hint" id="mWifiSub">home network</p></div>
</div>
<div class="card"><h2>Thread identity</h2><dl class="kv" id="overviewKv"></dl></div>
</section>
<section id="view-thread" class="grid hidden">
<div class="card"><h2>Thread network</h2>
<div id="netBanner" class="banner">Loading…</div>
<dl class="kv" id="netKv"></dl>
<label>Active dataset</label>
<input id="dataset" readonly>
<p class="hint">The portal sends this dataset when you pair a sensor. Leave the network from the serial console only if you intend to form a new one.</p>
</div>
</section>
<section id="view-devices" class="grid hidden">
<div class="card"><h2>Pair a device</h2>
<label for="mt">Matter setup code</label>
<input id="mt" autocomplete="off" autocapitalize="none" placeholder="MT:…">
<div class="row">
<div><label for="kind">Type</label>
<select id="kind">
<optgroup label="Sensors">
<option value="contact">Contact / door / window</option>
<option value="motion">Motion</option>
<option value="occupancy">Occupancy</option>
<option value="leak">Water leak</option>
<option value="climate">Temperature / humidity</option>
<option value="air">Air quality</option>
<option value="illuminance">Light level</option>
<option value="smoke">Smoke / CO</option>
</optgroup>
<optgroup label="Lights and power">
<option value="onoff">On/off light</option>
<option value="light">Dimmable light</option>
<option value="light_ct">White spectrum light</option>
<option value="light_color">Color light</option>
<option value="plug">Plug / outlet</option>
<option value="switch">Wall switch</option>
<option value="fan">Fan</option>
</optgroup>
<optgroup label="Controls">
<option value="button">Button / remote</option>
<option value="shade">Blind / shade</option>
<option value="lock">Door lock</option>
<option value="thermostat">Thermostat</option>
</optgroup>
</select></div>
<div><label for="dname">Name</label><input id="dname" maxlength="23" placeholder="kitchen"></div>
</div>
<label for="ep">Endpoint</label>
<input id="ep" value="1" inputmode="numeric">
<button type="button" class="btn" id="pairBtn">Pair device</button>
<p class="hint" id="pairMsg">Any Matter over Thread device. Factory-reset it so it advertises, then pair. IKEA, Eve, Aqara, Nanoleaf, and others use these types.</p>
</div>
<div class="card"><h2>Devices</h2><div id="devList"></div>
<p class="hint">Tap a device for details, rename, or remove.</p></div>
</section>
<section id="view-homekit" class="grid hidden">
<div class="card hk-setup"><h2>Add to Home</h2>
<div class="qrbox"><svg xmlns="http://www.w3.org/2000/svg" width="168" height="168" viewBox="0 0 21 21" shape-rendering="crispEdges"><rect width="21" height="21" fill="#fff"/><rect x="0" y="0" width="7" height="1" fill="#04120f"/><rect x="8" y="0" width="2" height="1" fill="#04120f"/><rect x="14" y="0" width="7" height="1" fill="#04120f"/><rect x="0" y="1" width="1" height="1" fill="#04120f"/><rect x="6" y="1" width="1" height="1" fill="#04120f"/><rect x="9" y="1" width="4" height="1" fill="#04120f"/><rect x="14" y="1" width="1" height="1" fill="#04120f"/><rect x="20" y="1" width="1" height="1" fill="#04120f"/><rect x="0" y="2" width="1" height="1" fill="#04120f"/><rect x="2" y="2" width="3" height="1" fill="#04120f"/><rect x="6" y="2" width="1" height="1" fill="#04120f"/><rect x="8" y="2" width="3" height="1" fill="#04120f"/><rect x="14" y="2" width="1" height="1" fill="#04120f"/><rect x="16" y="2" width="3" height="1" fill="#04120f"/><rect x="20" y="2" width="1" height="1" fill="#04120f"/><rect x="0" y="3" width="1" height="1" fill="#04120f"/><rect x="2" y="3" width="3" height="1" fill="#04120f"/><rect x="6" y="3" width="1" height="1" fill="#04120f"/><rect x="9" y="3" width="1" height="1" fill="#04120f"/><rect x="11" y="3" width="1" height="1" fill="#04120f"/><rect x="14" y="3" width="1" height="1" fill="#04120f"/><rect x="16" y="3" width="3" height="1" fill="#04120f"/><rect x="20" y="3" width="1" height="1" fill="#04120f"/><rect x="0" y="4" width="1" height="1" fill="#04120f"/><rect x="2" y="4" width="3" height="1" fill="#04120f"/><rect x="6" y="4" width="1" height="1" fill="#04120f"/><rect x="10" y="4" width="3" height="1" fill="#04120f"/><rect x="14" y="4" width="1" height="1" fill="#04120f"/><rect x="16" y="4" width="3" height="1" fill="#04120f"/><rect x="20" y="4" width="1" height="1" fill="#04120f"/><rect x="0" y="5" width="1" height="1" fill="#04120f"/><rect x="6" y="5" width="1" height="1" fill="#04120f"/><rect x="8" y="5" width="1" height="1" fill="#04120f"/><rect x="14" y="5" width="1" height="1" fill="#04120f"/><rect x="20" y="5" width="1" height="1" fill="#04120f"/><rect x="0" y="6" width="7" height="1" fill="#04120f"/><rect x="8" y="6" width="1" height="1" fill="#04120f"/><rect x="10" y="6" width="1" height="1" fill="#04120f"/><rect x="12" y="6" width="1" height="1" fill="#04120f"/><rect x="14" y="6" width="7" height="1" fill="#04120f"/><rect x="9" y="7" width="1" height="1" fill="#04120f"/><rect x="0" y="8" width="1" height="1" fill="#04120f"/><rect x="2" y="8" width="1" height="1" fill="#04120f"/><rect x="6" y="8" width="2" height="1" fill="#04120f"/><rect x="11" y="8" width="1" height="1" fill="#04120f"/><rect x="15" y="8" width="1" height="1" fill="#04120f"/><rect x="18" y="8" width="1" height="1" fill="#04120f"/><rect x="20" y="8" width="1" height="1" fill="#04120f"/><rect x="0" y="9" width="1" height="1" fill="#04120f"/><rect x="2" y="9" width="1" height="1" fill="#04120f"/><rect x="5" y="9" width="1" height="1" fill="#04120f"/><rect x="10" y="9" width="2" height="1" fill="#04120f"/><rect x="15" y="9" width="1" height="1" fill="#04120f"/><rect x="17" y="9" width="1" height="1" fill="#04120f"/><rect x="20" y="9" width="1" height="1" fill="#04120f"/><rect x="1" y="10" width="1" height="1" fill="#04120f"/><rect x="3" y="10" width="2" height="1" fill="#04120f"/><rect x="6" y="10" width="1" height="1" fill="#04120f"/><rect x="8" y="10" width="4" height="1" fill="#04120f"/><rect x="15" y="10" width="3" height="1" fill="#04120f"/><rect x="20" y="10" width="1" height="1" fill="#04120f"/><rect x="0" y="11" width="4" height="1" fill="#04120f"/><rect x="5" y="11" width="1" height="1" fill="#04120f"/><rect x="8" y="11" width="4" height="1" fill="#04120f"/><rect x="13" y="11" width="2" height="1" fill="#04120f"/><rect x="16" y="11" width="1" height="1" fill="#04120f"/><rect x="19" y="11" width="2" height="1" fill="#04120f"/><rect x="4" y="12" width="1" height="1" fill="#04120f"/><rect x="6" y="12" width="2" height="1" fill="#04120f"/><rect x="9" y="12" width="3" height="1" fill="#04120f"/><rect x="13" y="12" width="5" height="1" fill="#04120f"/><rect x="8" y="13" width="3" height="1" fill="#04120f"/><rect x="12" y="13" width="3" height="1" fill="#04120f"/><rect x="17" y="13" width="1" height="1" fill="#04120f"/><rect x="19" y="13" width="1" height="1" fill="#04120f"/><rect x="0" y="14" width="7" height="1" fill="#04120f"/><rect x="8" y="14" width="2" height="1" fill="#04120f"/><rect x="11" y="14" width="1" height="1" fill="#04120f"/><rect x="14" y="14" width="3" height="1" fill="#04120f"/><rect x="20" y="14" width="1" height="1" fill="#04120f"/><rect x="0" y="15" width="1" height="1" fill="#04120f"/><rect x="6" y="15" width="1" height="1" fill="#04120f"/><rect x="12" y="15" width="3" height="1" fill="#04120f"/><rect x="17" y="15" width="2" height="1" fill="#04120f"/><rect x="20" y="15" width="1" height="1" fill="#04120f"/><rect x="0" y="16" width="1" height="1" fill="#04120f"/><rect x="2" y="16" width="3" height="1" fill="#04120f"/><rect x="6" y="16" width="1" height="1" fill="#04120f"/><rect x="9" y="16" width="1" height="1" fill="#04120f"/><rect x="11" y="16" width="3" height="1" fill="#04120f"/><rect x="15" y="16" width="1" height="1" fill="#04120f"/><rect x="19" y="16" width="1" height="1" fill="#04120f"/><rect x="0" y="17" width="1" height="1" fill="#04120f"/><rect x="2" y="17" width="3" height="1" fill="#04120f"/><rect x="6" y="17" width="1" height="1" fill="#04120f"/><rect x="11" y="17" width="1" height="1" fill="#04120f"/><rect x="13" y="17" width="1" height="1" fill="#04120f"/><rect x="15" y="17" width="2" height="1" fill="#04120f"/><rect x="18" y="17" width="1" height="1" fill="#04120f"/><rect x="0" y="18" width="1" height="1" fill="#04120f"/><rect x="2" y="18" width="3" height="1" fill="#04120f"/><rect x="6" y="18" width="1" height="1" fill="#04120f"/><rect x="8" y="18" width="1" height="1" fill="#04120f"/><rect x="10" y="18" width="1" height="1" fill="#04120f"/><rect x="12" y="18" width="3" height="1" fill="#04120f"/><rect x="16" y="18" width="1" height="1" fill="#04120f"/><rect x="18" y="18" width="3" height="1" fill="#04120f"/><rect x="0" y="19" width="1" height="1" fill="#04120f"/><rect x="6" y="19" width="1" height="1" fill="#04120f"/><rect x="9" y="19" width="1" height="1" fill="#04120f"/><rect x="11" y="19" width="1" height="1" fill="#04120f"/><rect x="13" y="19" width="1" height="1" fill="#04120f"/><rect x="17" y="19" width="2" height="1" fill="#04120f"/><rect x="20" y="19" width="1" height="1" fill="#04120f"/><rect x="0" y="20" width="7" height="1" fill="#04120f"/><rect x="8" y="20" width="1" height="1" fill="#04120f"/><rect x="12" y="20" width="1" height="1" fill="#04120f"/><rect x="19" y="20" width="2" height="1" fill="#04120f"/></svg></div>
<p class="code" id="hkCode">111-22-333</p>
<p class="hint">Open the Home app, tap Add Accessory, and scan this code. The bridge is named ICC Thread Gateway.</p>
<p class="hint" id="hkUri"></p>
</div>
<div class="card"><h2>Bridge</h2>
<dl class="kv" id="hkKv"></dl>
<p class="hint">Devices you pair on the Devices tab show up in Home through this bridge.</p>
<button type="button" class="btn secondary" id="hkReset">Remove Home pairing</button>
<p class="hint">Use this after you delete the bridge in the Home app. The gateway restarts, then scan the code above. Wi-Fi, Thread, and paired sensors stay.</p>
</div>
</section>
<section id="view-sniff" class="grid hidden">
<div class="card">
<div class="card-head" style="display:flex;justify-content:space-between;align-items:center;gap:.6rem">
<h2 style="margin:0">Matter sniffer</h2>
<span id="sniffLive" class="pill">Live</span></div>
<p class="hint">Every Matter message is listed. EVT is an event the device sent on its own (a press, the window opening). RX is an attribute value. TX is a subscribe or read from this gateway.</p>
<div class="sniff-toolbar">
<button type="button" class="btn secondary sm" id="sniffPause">Pause</button>
<button type="button" class="btn secondary sm" id="sniffClear">Clear</button>
<label class="check"><input type="checkbox" id="sniffRx" checked> RX</label>
<label class="check"><input type="checkbox" id="sniffTx" checked> TX</label>
<label class="check"><input type="checkbox" id="sniffEvt" checked> EVT</label>
<label class="check"><input type="checkbox" id="sniffHideTime"> Hide battery</label>
<label class="check"><input type="checkbox" id="sniffAutoscroll" checked> Autoscroll</label>
<span class="hint" id="sniffCount" style="margin:0">0 frames</span>
</div>
<div class="sniff-log" id="sniffLog">Waiting for traffic…</div>
</div></section>
<section id="view-system" class="grid hidden">
<div class="card"><h2>Wi‑Fi</h2>
<p class="hint" id="wifiNow">—</p>
<button type="button" class="btn secondary" id="scanBtn">Scan</button>
<div class="ap-list" id="apList"></div>
<label for="ssid">SSID</label><input id="ssid" maxlength="32" autocomplete="off" autocapitalize="none">
<label for="password">Password</label><input id="password" type="password" maxlength="63">
<button type="button" class="btn" id="saveWifi">Save &amp; connect</button>
</div>
<div class="card"><h2>Diagnostics</h2><dl class="kv" id="diagKv"></dl></div>
<div class="card"><h2>Last reset</h2>
<dl class="kv" id="bootKv"></dl>
<pre class="bootlog" id="bootLog">—</pre>
<p class="hint">This is the reason the previous boot ended, plus the log kept across that restart.</p>
</div>
</section>
</main>
<footer>ICC Thread Gateway · <span id="fwVer">—</span></footer>
<div id="toasts"></div>
<div id="modalRoot" class="modal-backdrop hidden" role="dialog" aria-modal="true">
<div class="modal">
<div class="modal-head">
<h3 id="mdTitle">Device</h3>
<p class="sub" id="mdEui"></p>
</div>
<div class="modal-body">
<div class="modal-meta"><dl class="kv" id="mdKv"></dl></div>
<div class="modal-edit">
<label for="mdName">Friendly name</label>
<input id="mdName" maxlength="23" placeholder="Kitchen sensor">
<label for="mdKind">Type</label>
<select id="mdKind"></select>
<p class="hint">Changing the type rebuilds the HomeKit accessory so Home shows the right sensor.</p>
<div id="mdButtons" class="hidden">
<h4>Buttons</h4>
<p class="hint">Stateless sends a press. Stateful turns on and off with the same button. Several buttons show as one grouped accessory in Home.</p>
<div id="mdBtnList"></div>
</div>
</div>
</div>
<div class="modal-foot">
<div class="modal-actions">
<button type="button" class="btn" id="mdSave">Save</button>
<button type="button" class="btn secondary" id="mdRead">Read values</button>
<button type="button" class="btn secondary" id="mdClose">Close</button>
<button type="button" class="btn danger" id="mdRemove">Remove</button>
</div>
<p class="hint">Remove drops it from this gateway and from Home. The sensor stays on the Thread fabric until you factory-reset it.</p>
</div>
</div></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function toast(m,kind){const t=document.createElement('div');t.className='toast '+(kind||'');t.textContent=m;
$('#toasts').appendChild(t);setTimeout(()=>t.remove(),2400)}
async function api(path,opts){const r=await fetch(path,opts);const text=await r.text();
let body=null;try{body=JSON.parse(text)}catch(e){}
if(!r.ok)throw new Error((body&&body.error)||text||r.status);return body}
function kv(el,rows){el.innerHTML=rows.filter(r=>r&&r[1]!=null&&r[1]!=='').map(([k,v])=>'<dt>'+k+'</dt><dd>'+v+'</dd>').join('')}
let S=null,mdId=null;
function findNode(id){return ((S&&S.nodes)||[]).find(n=>String(n.node_id)===String(id))}
function closeModal(){const root=$('#modalRoot');if(root)root.classList.add('hidden');mdId=null}
function renderButtons(n,soft){
 const box=$('#mdButtons'),list=$('#mdBtnList');
 if(!box||!list)return;
 if(($('#mdKind').value||n.kind)!=='button'){box.classList.add('hidden');return}
 box.classList.remove('hidden');
 const buttons=n.buttons||[];
 if(!buttons.length){list.innerHTML='<p class="hint">Press each button once so the gateway can learn it.</p>';return}
 if(soft&&document.activeElement&&list.contains(document.activeElement))return;
 if(soft&&list.querySelectorAll('.btn-row').length===buttons.length)return;
 list.innerHTML=buttons.map((b,i)=>'<div class="btn-row"><input maxlength="23" data-bn="'+i+'" value="'+esc(b.name||('Button '+(i+1)))+'">'+
  '<select data-bm="'+i+'"><option value="0"'+(Number(b.mode)===1?'':' selected')+'>Stateless</option>'+
  '<option value="1"'+(Number(b.mode)===1?' selected':'')+'>Stateful switch</option></select></div>').join('');
}
function openModal(id,soft){
 const n=findNode(id);if(!n){closeModal();return}
 const already=mdId===String(n.node_id)&&!$('#modalRoot').classList.contains('hidden');
 mdId=String(n.node_id);
 $('#modalRoot').classList.remove('hidden');
 $('#mdEui').textContent='node '+n.node_id+' · endpoint '+n.endpoint;
 if(!soft||!already){
  $('#mdTitle').textContent=n.name||'Device';
  $('#mdName').value=n.name||'';
  $('#mdKind').value=n.kind||'contact';
  renderButtons(n,false);
 } else renderButtons(n,true);
 kv($('#mdKv'),[
  ['Type',esc(n.kind)],['State',esc(n.state||'waiting')],
  ['Node',esc(n.node_id)],['Endpoint',esc(n.endpoint)],
  ['Signal',signalHtml(n.rssi)],
  ['Battery',n.battery>=0?esc(n.battery)+' %':null],
  ['Temperature',(n.temp_c!=null&&n.temp_c>-100)?Number(n.temp_c).toFixed(1)+' °C':null],
  ['Humidity',(n.humidity!=null&&n.humidity>=0)?Number(n.humidity).toFixed(0)+' %':null],
  ['HomeKit','bridged']]);
}
function renderModal(){if(mdId)openModal(mdId,true)}
let sniffTimer=null,sniffPaused=false,sniffSeq=-1,sniffActive=false,sniffBusy=false;
function clusterLabel(c){
 const m={0x45:'Boolean State',0x406:'Occupancy',0x3b:'Switch',0x2f:'Power Source',0x402:'Temperature',
  0x405:'Humidity',0x6:'On/Off',0x8:'Level',0x300:'Color',0x102:'Window Covering',0x201:'Thermostat',
  0x403:'Illuminance',0x101:'Door Lock'};
 return m[c]||(c?('cluster 0x'+Number(c).toString(16)):'');
}
function sniffDevLabel(node){
 const n=(S&&S.nodes||[]).find(x=>String(x.node_id)===String(node));
 return n?String(n.name||n.kind||''):'';
}
function renderSniff(entries){
 const showRx=$('#sniffRx').checked,showTx=$('#sniffTx').checked,showEvt=$('#sniffEvt').checked;
 const hideBat=$('#sniffHideTime').checked;
 const filtered=entries.filter(e=>{
  if(hideBat&&e.cluster===0x2f)return false;
  return (e.dir==='RX'&&showRx)||(e.dir==='TX'&&showTx)||(e.dir==='EVT'&&showEvt)});
 $('#sniffCount').textContent=filtered.length+' / '+entries.length+' frames';
 const el=$('#sniffLog');
 if(!filtered.length){el.textContent='No frames yet.';return}
 const stick=el.scrollHeight-el.scrollTop-el.clientHeight<40;
 el.innerHTML=filtered.map(e=>{
  const cls=e.dir.toLowerCase();
  const t=(e.t/1000).toFixed(3)+'s';
  let line='<span class="'+cls+'">'+e.dir+'</span> <span class="meta">'+t+'</span> ';
  const nm=sniffDevLabel(e.node);
  if(nm)line+='<span class="meta">['+esc(nm)+']</span> ';
  line+='node '+esc(e.node);
  const cl=clusterLabel(e.cluster);if(cl)line+=' '+esc(cl);
  if(e.attr)line+=' <span class="meta">attr 0x'+Number(e.attr).toString(16)+'</span>';
  if(e.src_ep)line+=' ep='+esc(e.src_ep);
  if(e.hex)line+='\n  '+esc(e.hex);
  return line}).join('\n');
 if($('#sniffAutoscroll').checked&&stick)el.scrollTop=el.scrollHeight;
}
async function pollSniff(){
 if(!sniffActive||sniffPaused||sniffBusy)return;
 sniffBusy=true;
 try{const q=sniffSeq<0?'':'?since='+sniffSeq;
  const j=await api('/api/matter/sniff'+q);
  if(j.unchanged)sniffSeq=j.seq;
  else if(j.seq!=null){sniffSeq=j.seq;renderSniff(j.entries||[])}
 }catch(e){} finally{sniffBusy=false}}
function startSniff(){sniffActive=true;$('#sniffLive').classList.add('on');$('#sniffLive').textContent=sniffPaused?'Paused':'Live';
 if(!sniffTimer)sniffTimer=setInterval(pollSniff,2500);pollSniff()}
function stopSniff(){sniffActive=false;$('#sniffLive').classList.remove('on');
 if(sniffTimer){clearInterval(sniffTimer);sniffTimer=null}}
function showTab(name){document.querySelectorAll('main>section').forEach(s=>s.classList.add('hidden'));
$('#view-'+name).classList.remove('hidden');
document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('active',b.dataset.tab===name));
if(name==='sniff')startSniff();else stopSniff()}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>showTab(b.dataset.tab));
function chip(text){return '<span class="chip">'+esc(text)+'</span>'}
function prettyState(text){
 const s=String(text||'').trim();
 if(!s)return '';
 const map={DETECTED:'Motion',CLEAR:'Clear',LEAK:'Leak',DRY:'Dry',ON:'On',OFF:'Off',
  OCCUPIED:'Occupied',LOCKED:'Locked',UNLOCKED:'Unlocked'};
 return map[s]||(s.charAt(0).toUpperCase()+s.slice(1).toLowerCase());
}
function kindLabel(n){
 const k=n.kind;
 if(k==='button'){const c=(n.buttons||[]).length;return c?('Remote · '+c+' buttons'):'Remote control'}
 if(k==='contact')return 'Contact sensor';
 if(k==='motion')return 'Motion sensor';
 if(k==='occupancy')return 'Occupancy sensor';
 if(k==='leak')return 'Leak sensor';
 if(k==='smoke')return 'Smoke sensor';
 if(k==='climate'){
  if(n.temp_c>-100&&n.humidity>=0)return 'Temperature & humidity sensor';
  if(n.temp_c>-100)return 'Temperature sensor';
  if(n.humidity>=0)return 'Humidity sensor';
  return 'Climate sensor'}
 if(k==='air')return 'Air quality sensor';
 if(k==='illuminance')return 'Light sensor';
 if(k==='plug')return 'Smart outlet';
 if(k==='light'||k==='light_ct'||k==='light_color'||k==='onoff')return 'Light / dimmer';
 if(k==='switch')return 'Smart switch';
 if(k==='fan')return 'Fan';
 if(k==='shade')return 'Window covering';
 if(k==='lock')return 'Lock';
 if(k==='thermostat')return 'Thermostat';
 return k||'Device';
}
function signalHtml(rssi){
 const known=rssi!=null&&rssi!==''&&Number.isFinite(Number(rssi))&&Number(rssi)<127;
 const r=known?Number(rssi):null;
 const lvl=r==null?0:(r>=-50?4:r>=-65?3:r>=-78?2:r>=-90?1:1);
 return '<span class="wifi-wrap" title="'+(r==null?'No signal yet':(r+' dBm'))+'"><span class="wifi" data-lvl="'+lvl+'" aria-hidden="true"><i></i><i></i><i></i><b></b></span>'+(r==null?'':'<span>'+r+' dBm</span>')+'</span>';
}
function deviceChips(n){
 const chips=[];
 const k=n.kind;
 const numeric=k==='climate'||k==='air'||k==='thermostat'||k==='illuminance';
 if(k==='button'){
  const buttons=n.buttons||[];
  chips.push(chip(buttons.length?(buttons.length+' btn'):'Remote'));
  buttons.forEach(b=>{
   if(Number(b.mode)===1)chips.push(chip((b.name||'Button')+' '+(Number(b.on)?'on':'off')));
  });
 }else if(!numeric&&n.state&&n.state!=='waiting'){
  String(n.state).split(' · ').forEach(part=>{
   const label=prettyState(part);
   if(label)chips.push(chip(label));
  });
 }
 if(n.temp_c!=null&&n.temp_c>-100)chips.push(chip(Number(n.temp_c).toFixed(1)+'°C'));
 if(n.humidity!=null&&n.humidity>=0)chips.push(chip(Number(n.humidity).toFixed(0)+'%'));
 if(n.battery>=0)chips.push(chip('bat '+n.battery+'%'));
 chips.push(chip('HomeKit'));
 return chips.join('');
}
function fill(st){
  S=st;
  const th=st.thread||{},w=st.wifi||{},b=st.board||{};
  $('#fwVer').textContent=st.fw_version||'—';
  const role=th.role||'down';
  const pill=$('#topPill');
  pill.textContent=role;
  pill.className='pill '+(role==='leader'||role==='router'||role==='child'?'ok':(role==='detached'?'warn':'bad'));
  $('#mIcc').textContent=th.rcp==='ok'?'CONNECTED':'NO RCP';
  $('#mIccSub').textContent='UART TX '+b.uart_tx+' / RX '+b.uart_rx+' · '+b.uart_baud;
  $('#mNet').textContent=th.network_name||role;
  $('#mNetSub').textContent='ch '+(th.channel||'—')+' · PAN '+(th.pan_id||'—');
  const nodes=st.nodes||[];
  $('#mDev').textContent=String(nodes.length);
  $('#mDevSub').textContent=nodes.length?nodes.map(n=>(n.name||n.kind)+' '+String(n.state||'waiting')).join(' · '):'paired on this fabric';
  $('#mWifi').textContent=w.sta_connected?(w.ssid||'connected'):'—';
  $('#mWifiSub').textContent=w.sta_connected?((w.ip||'')+' · '+(w.rssi||'')+' dBm'):'not connected';
  $('#wifiNow').textContent=w.sta_connected?(w.ssid+' · '+w.ip+' · '+w.rssi+' dBm'):'Not connected';
  $('#pairMsg').textContent=st.pairing||'Factory-reset the device so it advertises, then pair.';
  const rows=[['Network',th.network_name||'—'],['Role',role],['Channel',th.channel||'—'],
    ['PAN',th.pan_id||'—'],['Ext PAN',th.ext_pan||'—'],['Partition',th.partition||'—']];
  kv($('#overviewKv'),rows);kv($('#netKv'),rows);
  const hk=st.homekit||{};
  $('#hkCode').textContent=hk.setup_code||'111-22-333';
  $('#hkUri').textContent=hk.setup_uri||'';
  kv($('#hkKv'),[['Name','ICC Thread Gateway'],['Setup code',hk.setup_code||'111-22-333'],
    ['Setup ID',hk.setup_id||'THRD'],['Setup URI',hk.setup_uri||'—'],
    ['Status',hk.started?(hk.paired?'Paired with a home':'Ready to add'):'Starting'],
    ['Bridged accessories',hk.accessories||0]]);
  $('#dataset').value=th.dataset||'';
  $('#netBanner').className='banner '+(th.rcp==='ok'?'ok':'warn');
  $('#netBanner').textContent=th.rcp==='ok'
    ?('Thread is up as '+role+' on '+(th.network_name||'the dataset')+'.')
    :'The ICC RCP is not attached yet.';
  const list=$('#devList');
  if(!nodes.length){list.innerHTML='<div class="empty"><strong>No sensors yet</strong>Pair a factory-reset IKEA sensor with its MT: code.</div>'}
  else list.innerHTML=nodes.map(n=>'<div class="device" data-id="'+esc(n.node_id)+'"><div style="min-width:0;flex:1"><div class="title"><span class="title-text">'+esc(n.name)+'</span>'+signalHtml(n.rssi)+'</div>'
    +'<div class="id">node '+esc(n.node_id)+'</div>'
    +'<div class="kind">'+esc(kindLabel(n))+'</div>'
    +'<div class="meta">endpoint '+esc(n.endpoint)+'</div>'
    +'<div class="vals">'+deviceChips(n)+'</div></div>'
    +'<button type="button" class="btn secondary sm"><span class="label">Details</span></button></div>').join('');
  renderModal();
  const boot=st.boot||{};
  kv($('#bootKv'),[['This boot',esc(boot.reset||'—')],['Last panic',esc(boot.panic||'none')]]);
  $('#bootLog').textContent=(boot.log&&boot.log.length)?boot.log:'No log was kept from the previous boot.';
  kv($('#diagKv'),[['Chip',b.chip||'—'],['UART',b.uart_tx+' / '+b.uart_rx+' @ '+b.uart_baud],
    ['Uptime',st.uptime||'—'],['Free heap',st.free_heap||'—'],
    ['Internal',st.heap_internal||'—'],['PSRAM',st.heap_psram||'—'],
    ['Firmware',st.fw_version||'—']]);
}
$('#mdKind').innerHTML=$('#kind').innerHTML;
$('#devList').onclick=ev=>{const row=ev.target.closest&&ev.target.closest('.device');if(row&&row.dataset.id)openModal(row.dataset.id,false)};
$('#mdClose').onclick=closeModal;
$('#modalRoot').onclick=ev=>{if(ev.target===$('#modalRoot'))closeModal()};
document.addEventListener('keydown',ev=>{if(ev.key==='Escape')closeModal()});
$('#mdKind').onchange=()=>{const n=findNode(mdId);if(n)renderButtons(Object.assign({},n,{kind:$('#mdKind').value}),false)};
$('#mdSave').onclick=async()=>{if(!mdId)return;const name=$('#mdName').value.trim();const kind=$('#mdKind').value;
  const buttons=[...document.querySelectorAll('#mdBtnList .btn-row')].map(row=>({
    name:row.querySelector('input').value.trim(),mode:Number(row.querySelector('select').value)}));
  const payload={node_id:Number(mdId),name,kind};
  if(kind==='button'&&buttons.length)payload.buttons=buttons;
  $('#mdSave').disabled=true;
  try{await api('/api/nodes/update',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)});
    $('#mdTitle').textContent=name||'Device';toast('Saved','ok');refresh()}catch(e){toast(e.message,'err')}
  $('#mdSave').disabled=false};
$('#mdRead').onclick=async()=>{if(!mdId)return;
  try{await api('/api/nodes/read',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({node_id:Number(mdId)})});toast('Reading…','ok');setTimeout(refresh,700)}catch(e){toast(e.message,'err')}};
$('#sniffPause').onclick=()=>{sniffPaused=!sniffPaused;$('#sniffPause').textContent=sniffPaused?'Resume':'Pause';
 $('#sniffLive').textContent=sniffPaused?'Paused':'Live';if(!sniffPaused)pollSniff()};
$('#sniffClear').onclick=async()=>{try{await api('/api/matter/sniff/clear',{method:'POST'});sniffSeq=-1;
 $('#sniffLog').textContent='Waiting for traffic…';$('#sniffCount').textContent='0 frames'}catch(e){toast(e.message,'err')}};
['sniffRx','sniffTx','sniffEvt','sniffHideTime'].forEach(id=>{const el=$('#'+id);if(el)el.onchange=()=>pollSniff()});
$('#hkReset').onclick=async()=>{
  if(!confirm('Remove this bridge from Home? You can add it again with the code above.'))return;
  $('#hkReset').disabled=true;
  try{await api('/api/homekit/reset',{method:'POST'});toast('Pairing cleared. The gateway will restart.','ok')}
  catch(e){toast(e.message,'err');$('#hkReset').disabled=false}};
$('#mdRemove').onclick=async()=>{if(!mdId)return;
  if(!confirm('Remove this device from the gateway and Home?'))return;
  $('#mdRemove').disabled=true;
  try{await api('/api/nodes/forget',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({node_id:Number(mdId)})});toast('Removed','ok');closeModal();refresh()}catch(e){toast(e.message,'err')}
  $('#mdRemove').disabled=false};
$('#pairBtn').onclick=async()=>{const payload=$('#mt').value.trim();if(!payload){toast('Enter the MT: code','err');return}
  $('#pairBtn').disabled=true;
  try{await api('/api/nodes/pair',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({payload,kind:$('#kind').value,name:$('#dname').value.trim(),endpoint:Number($('#ep').value||1)})});
    toast('Pairing started','ok');refresh()}catch(e){toast(e.message,'err')}
  $('#pairBtn').disabled=false};
$('#scanBtn').onclick=async()=>{try{const j=await api('/api/wifi/scan');
  $('#apList').innerHTML=(j.aps||[]).map(a=>'<div class="ap-row"><div><div class="ap-ssid">'+esc(a.ssid)+'</div>'
    +'<div class="ap-detail">'+esc(a.bssid)+' · '+esc(a.rssi)+' dBm</div></div>'
    +'<button type="button" class="btn secondary sm" data-ssid="'+esc(a.ssid)+'">Use</button></div>').join('')
    ||'<p class="hint">No access points</p>'}catch(e){toast(e.message,'err')}};
$('#apList').onclick=ev=>{const s=ev.target&&ev.target.dataset&&ev.target.dataset.ssid;if(s)$('#ssid').value=s};
$('#saveWifi').onclick=async()=>{const ssid=$('#ssid').value.trim();if(!ssid){toast('SSID required','err');return}
  try{await api('/api/wifi/sta',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid,password:$('#password').value})});toast('Connecting…','ok')}catch(e){toast(e.message,'err')}};
async function refresh(){try{fill(await api('/api/status'))}catch(e){toast('status: '+e.message,'err')}}
refresh();setInterval(refresh,4000);
</script></body></html>)HTML";
