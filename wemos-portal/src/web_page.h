#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <pgmspace.h>

/* A pagina do portal, inteira, em flash. Servida pelo Wemos D1 mini; os dados
 * vem do CYD pelo fio serial (ver link_proto.h).
 *
 * Nada de CDN: o celular entra num AP que nao tem internet, entao qualquer
 * <script src> externo ficaria pendurado ate dar timeout e a pagina abriria
 * quebrada. Fonte do sistema, CSS e JS inline, grafico desenhado no canvas na
 * mao — sem biblioteca.
 *
 * Servida com send_P direto do PROGMEM, sem copiar para a RAM: com WiFi e
 * Bluetooth no ar o heap e o recurso apertado, e montar isto numa String
 * custaria o dobro do tamanho em memoria fragmentada.
 *
 * Aqui pode haver acento: isto e UTF-8 no navegador, nao as fontes Montserrat
 * do LVGL, que nao tem glifo acentuado. */

static const char WEB_PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Car Dashboard</title>
<style>
:root{
  --bg:#0b0b0c; --card:#1c1c1c; --line:#2a2a2c; --text:#fff; --muted:#8b96a5;
  --ok:#2fd27a; --warn:#ffc233; --bad:#ff4d4d; --accent:#2f6fdb; --danger:#9b1c1c;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
  font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
  padding:16px 16px calc(32px + env(safe-area-inset-bottom));max-width:720px;margin:0 auto}
h1{font-size:20px;margin:0}
h2{font-size:13px;letter-spacing:.09em;text-transform:uppercase;color:var(--muted);
  margin:28px 0 10px;font-weight:600}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:4px}
.sub{color:var(--muted);font-size:13px}
.pill{font-size:12px;padding:5px 11px;border-radius:999px;background:var(--card);
  border:1px solid var(--line);white-space:nowrap}
.pill.on{color:var(--ok);border-color:#1e5c3c}
.pill.off{color:var(--bad);border-color:#5c2020}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
.card .k{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}
.card .v{font-size:27px;font-variant-numeric:tabular-nums;margin-top:3px;line-height:1.1}
.card .v small{font-size:14px;color:var(--muted);margin-left:3px}
.card .m{font-size:12px;color:var(--muted);margin-top:4px;font-variant-numeric:tabular-nums}
.chips{display:flex;gap:7px;overflow-x:auto;padding-bottom:7px;-webkit-overflow-scrolling:touch}
.chip{flex:0 0 auto;font-size:13px;padding:7px 13px;border-radius:999px;cursor:pointer;
  background:var(--card);border:1px solid var(--line);color:var(--muted)}
.chip[aria-selected=true]{background:var(--accent);border-color:var(--accent);color:#fff}
.chartbox{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px}
canvas{width:100%;height:190px;display:block}
.stats{display:flex;gap:18px;margin-top:9px;font-size:12px;color:var(--muted);
  font-variant-numeric:tabular-nums;flex-wrap:wrap}
.stats b{color:var(--text);font-weight:600}
.dtc{background:var(--card);border:1px solid var(--line);border-left-width:3px;
  border-radius:10px;padding:11px 13px;margin-bottom:8px}
.dtc.stored{border-left-color:var(--bad)} .dtc.pending{border-left-color:var(--warn)}
.dtc.permanent{border-left-color:var(--muted)}
.dtc .c{font-size:19px;font-weight:600;font-variant-numeric:tabular-nums}
.dtc .t{float:right;font-size:11px;letter-spacing:.06em;color:var(--muted);margin-top:5px}
.dtc .d{font-size:13px;color:var(--muted);margin-top:3px;clear:both}
.row{display:flex;gap:10px;margin-top:12px}
button{flex:1;font:inherit;font-size:15px;padding:13px;border:0;border-radius:10px;
  color:#fff;background:var(--accent);cursor:pointer}
button.danger{background:var(--danger)}
button:disabled{background:#333;color:var(--muted);cursor:default}
.note{font-size:13px;color:var(--muted);margin-top:10px}
.empty{color:var(--muted);font-size:14px;padding:12px 0}
dialog{background:var(--card);color:var(--text);border:1px solid var(--danger);
  border-radius:14px;padding:20px;max-width:330px;width:calc(100% - 40px)}
dialog::backdrop{background:#000b}
dialog h3{margin:0 0 10px;font-size:17px}
dialog p{margin:0;font-size:14px;color:var(--muted)}
</style>
</head>
<body>

<header>
  <div>
    <h1>Car Dashboard</h1>
    <div class="sub" id="profile">&nbsp;</div>
  </div>
  <div class="pill" id="link">conectando…</div>
</header>

<h2>Agora</h2>
<div class="grid" id="gauges"></div>

<h2>Histórico da sessão</h2>
<div class="chips" id="chips" role="tablist"></div>
<div class="chartbox">
  <canvas id="chart"></canvas>
  <div class="stats" id="stats"></div>
</div>

<h2>Códigos de falha</h2>
<div id="dtcstatus" class="sub"></div>
<div id="dtclist"></div>
<div class="row">
  <button id="read">Ler códigos</button>
  <button id="clear" class="danger" disabled>Apagar</button>
</div>
<div class="note" id="dtcnote"></div>

<dialog id="confirm">
  <h3>Apagar os códigos?</h3>
  <p>Também zera o freeze frame e os monitores de prontidão — o carro leva alguns ciclos
  de condução para refazê-los. Faça com a ignição ligada e o motor desligado.
  Códigos permanentes não saem.</p>
  <div class="row">
    <button id="cno">Cancelar</button>
    <button id="cyes" class="danger">Apagar</button>
  </div>
</dialog>

<script>
const M = [
  {k:'boost', n:'Boost',      u:'bar', d:2},
  {k:'comb',  n:'Rampa comb.',u:'bar', d:1},
  {k:'lambda',n:'Lambda',     u:'',    d:2},
  {k:'iat',   n:'Admissão',   u:'°C',  d:0},
  {k:'oil',   n:'Óleo',       u:'°C',  d:0},
  {k:'water', n:'Água',       u:'°C',  d:0},
  {k:'batt',  n:'Bateria',    u:'V',   d:1},
];
const $ = s => document.querySelector(s);
const fmt = (v,d) => (v===null||v===undefined||!isFinite(v)) ? '--' : v.toFixed(d);
let sel = 'boost', hist = null, codes = [], seenRead = -1;

// ---- cartões ----
$('#gauges').innerHTML = M.map(m =>
  `<div class="card"><div class="k">${m.n}</div>
   <div class="v"><span id="v_${m.k}">--</span>${m.u?`<small>${m.u}</small>`:''}</div>
   <div class="m">máx <span id="x_${m.k}">--</span></div></div>`).join('');

$('#chips').innerHTML = M.map(m =>
  `<button class="chip" role="tab" data-k="${m.k}" aria-selected="${m.k===sel}">${m.n}</button>`).join('');
$('#chips').onclick = e => {
  const b = e.target.closest('.chip'); if(!b) return;
  sel = b.dataset.k;
  document.querySelectorAll('.chip').forEach(c => c.setAttribute('aria-selected', c.dataset.k===sel));
  draw();
};

// ---- estado ----
// Um pedido por vez, sempre. O servidor do painel atende um cliente de cada
// vez e cada fetch abre uma conexão TCP nova; com setInterval, quando o rádio
// (dividido com o Bluetooth do ELM327) ficava lento, as consultas empilhavam,
// estouravam a fila de 4 conexões do servidor e cada conexão pendurada comia
// memória que o próprio Wi-Fi precisava — a conexão ficava intermitente.
async function get(path, ms){
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), ms);
  try{
    const r = await fetch(path, {cache:'no-store', signal:ctl.signal, method: path.startsWith('/api/dtc/') ? 'POST' : 'GET'});
    if(!r.ok) throw new Error(r.status);
    return await r.json();
  }finally{ clearTimeout(t); }
}

function applyState(s){
  const link = $('#link');
  // cyd:false vem do Wemos quando o painel parou de mandar dados pelo fio.
  link.textContent = s.cyd === false ? 'painel sem resposta'
    : s.link ? 'ELM327 conectado' : (s.bt ? 'inicializando…' : 'sem ELM327');
  link.className = 'pill ' + (s.link ? 'on' : 'off');
  $('#profile').textContent = s.profile || '';
  for(const m of M){
    $('#v_'+m.k).textContent = s.valid ? fmt(s.g[m.k], m.d) : '--';
    $('#x_'+m.k).textContent = s.valid ? fmt(s.max[m.k], m.d) : '--';
  }
  renderDtc(s);
}

let fails = 0, histAt = 0, action = null;

async function tick(){
  try{
    // Botão de DTC entra na fila em vez de abrir uma conexão paralela. Pedido
    // com mais de 15 s é descartado: com a tela do celular apagada as consultas
    // param, e um "Apagar" confirmado não pode rodar minutos depois, quando o
    // motor já pode estar ligado.
    // Se o rádio derrubar o pedido, ele volta para a fila (ainda dentro dos
    // 15 s): clique não some em silêncio. Repetir é inofensivo — ler é ler, e
    // um segundo modo 04 só encontra a ECU já limpa.
    if(action){
      const a = action; action = null;
      if(Date.now() - a.at < 15000){
        try{ await get(a.path, 4000); }
        catch(e){ if(!action) action = a; throw e; }
      }
    }

    const s = await get('/api/state', 4000);
    fails = 0;

    // A lista de códigos é grande e muda raramente: só vai ao firmware quando
    // houve leitura nova. seenRead só avança se a busca deu certo.
    if(s.dtc.valid && s.dtc.op === 0 && s.dtc.read_ms !== seenRead){
      codes = (await get('/api/dtc', 6000)).codes || [];
      seenRead = s.dtc.read_ms;
    }
    applyState(s);

    if(Date.now() - histAt > 5000){
      hist = await get('/api/history', 10000);
      histAt = Date.now();
      draw();
    }
  }catch(e){
    // Uma falha isolada é o rádio ocupado; só três seguidas viram "sem conexão".
    if(++fails >= 3){ $('#link').textContent = 'sem conexão'; $('#link').className = 'pill off'; }
  }
  // Com falhas, espaça as tentativas em vez de insistir em cima de um link ruim.
  // 350 ms entre a resposta e o próximo pedido: com o CYD mandando o estado a
  // cada 250 ms, o celular acompanha o painel em vez de mostrar um retrato velho.
  const wait = fails ? Math.min(350 * 2 ** fails, 5000) : 350;
  setTimeout(() => { if(!document.hidden) tick(); else idle = true; }, wait);
}

// Tela do celular apagada ou aba em segundo plano: para de consultar, o rádio
// volta inteiro para o Bluetooth. Ao voltar, retoma na hora.
let idle = false;
document.addEventListener('visibilitychange', () => {
  if(!document.hidden && idle){ idle = false; tick(); }
});

// ---- gráfico ----
function draw(){
  const cv = $('#chart'), ctx = cv.getContext('2d');
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr; ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,W,H);

  const meta = M.find(m => m.k===sel);
  const data = hist && hist.series ? hist.series[sel] : null;
  if(!data || data.length < 2){
    ctx.fillStyle = '#8b96a5'; ctx.font = '14px sans-serif'; ctx.textAlign = 'center';
    ctx.fillText('aguardando leituras…', W/2, H/2);
    $('#stats').innerHTML = '';
    return;
  }

  let lo = Math.min(...data), hi = Math.max(...data);
  if(hi - lo < 1e-6){ hi += 1; lo -= 1; }           // série constante: não divide por zero
  const pad = (hi-lo)*0.12; lo -= pad; hi += pad;
  const PL = 44, PR = 8, PT = 10, PB = 18;
  const x = i => PL + i*(W-PL-PR)/(data.length-1);
  const y = v => PT + (hi-v)*(H-PT-PB)/(hi-lo);

  ctx.strokeStyle = '#2a2a2c'; ctx.fillStyle = '#8b96a5';
  ctx.font = '11px sans-serif'; ctx.textAlign = 'right'; ctx.lineWidth = 1;
  for(let i=0;i<=3;i++){
    const v = hi - (hi-lo)*i/3, yy = Math.round(y(v))+0.5;
    ctx.beginPath(); ctx.moveTo(PL,yy); ctx.lineTo(W-PR,yy); ctx.stroke();
    ctx.fillText(v.toFixed(meta.d), PL-6, yy+4);
  }

  // área sob a curva, só para dar leitura de volume
  ctx.beginPath(); ctx.moveTo(x(0), y(data[0]));
  for(let i=1;i<data.length;i++) ctx.lineTo(x(i), y(data[i]));
  ctx.lineTo(x(data.length-1), H-PB); ctx.lineTo(x(0), H-PB); ctx.closePath();
  const grd = ctx.createLinearGradient(0,PT,0,H-PB);
  grd.addColorStop(0,'rgba(47,111,219,.34)'); grd.addColorStop(1,'rgba(47,111,219,0)');
  ctx.fillStyle = grd; ctx.fill();

  ctx.beginPath(); ctx.moveTo(x(0), y(data[0]));
  for(let i=1;i<data.length;i++) ctx.lineTo(x(i), y(data[i]));
  ctx.strokeStyle = '#4a90ff'; ctx.lineWidth = 2;
  ctx.lineJoin = 'round'; ctx.lineCap = 'round'; ctx.stroke();

  const last = data[data.length-1];
  ctx.beginPath(); ctx.arc(x(data.length-1), y(last), 3.5, 0, 7); ctx.fillStyle = '#4a90ff'; ctx.fill();

  const mn = Math.min(...data), mx = Math.max(...data);
  const avg = data.reduce((a,b)=>a+b,0)/data.length;
  const mins = Math.round(data.length * (hist.interval_ms/1000) / 60);
  $('#stats').innerHTML =
    `<span>mín <b>${fmt(mn,meta.d)}</b></span><span>méd <b>${fmt(avg,meta.d)}</b></span>` +
    `<span>máx <b>${fmt(mx,meta.d)}</b></span>` +
    `<span>${data.length} leituras · ${mins} min</span>`;
}
addEventListener('resize', draw);

// ---- DTC ----
function renderDtc(s){
  const d = s.dtc, busy = d.op !== 0;
  const total = d.stored + d.pending + d.permanent;
  $('#dtcstatus').textContent = !s.link ? 'Conecte o ELM327 para ler os códigos.'
    : busy ? (d.op===2 ? 'Apagando…' : 'Lendo…')
    : !d.valid ? 'Ainda não lido.'
    : `MIL ${d.mil?'ACESA':'apagada'} · ${total} código${total===1?'':'s'}`;

  $('#read').disabled = !s.link || busy;
  $('#clear').disabled = !s.link || busy || !d.valid || (d.stored + d.pending) === 0;

  const TAG = {stored:'ARMAZENADO', pending:'PENDENTE', permanent:'PERMANENTE'};
  $('#dtclist').innerHTML = !d.valid || busy ? ''
    : total === 0 ? '<div class="empty">Nenhum código de falha.</div>'
    : codes.map(c => `<div class="dtc ${c.t}">
        <span class="c">${c.c}</span><span class="t">${TAG[c.t]||''}</span>
        <div class="d">${c.d}</div></div>`).join('');

  const note = {1:'Códigos apagados.',
    2:`A ECU recusou o apagamento${d.nrc===0x22?': condições não atendidas. Desligue o motor e mantenha a ignição ligada.':` (0x${d.nrc.toString(16)}).`}`,
    3:'Sem resposta da ECU ao apagar.'}[d.clear];
  $('#dtcnote').textContent = note || '';
}

const queue = path => { action = {path, at: Date.now()}; };
$('#read').onclick = () => queue('/api/dtc/read');
$('#clear').onclick = () => $('#confirm').showModal();
$('#cno').onclick = () => $('#confirm').close();
$('#cyes').onclick = () => { $('#confirm').close(); queue('/api/dtc/clear'); };

tick();
</script>
</body>
</html>
)HTML";

#endif
