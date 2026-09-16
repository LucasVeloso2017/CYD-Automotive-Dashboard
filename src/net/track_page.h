// Pagina servida em http://192.168.4.1/ pelo track_server. Autocontida de
// proposito: o celular esta na rede do ESP, sem internet, entao nada de CDN,
// fonte externa ou biblioteca. Mostra o RESUMO da ultima sessao (max/min,
// duracao, amostras) e os codigos de falha da ultima leitura (/dtc), com
// decodificacao estrutural SAE em JS: enquanto o Wi-Fi esta no ar o
// Bluetooth fica suspenso, entao nao ha dado ao vivo para mostrar.
//
// Para editar: cole o conteudo num .html, ajuste, teste, cole de volta.
#ifndef TRACK_PAGE_H
#define TRACK_PAGE_H

#include <pgmspace.h>

static const char TRACK_PAGE_HTML[] PROGMEM = R"html(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d10">
<title>Track Metrics</title>
<style>
:root{--bg:#0b0d10;--card:#151920;--line:#232a34;--tx:#e8ecf1;--mut:#8b96a5;--acc:#ff7a00;--ok:#2fd27a;--warn:#ffc233;--bad:#ff4d4d}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg);color:var(--tx);font:15px/1.35 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;-webkit-text-size-adjust:100%}
body{padding:max(12px,env(safe-area-inset-top)) 12px max(12px,env(safe-area-inset-bottom))}
header{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:4px 2px 12px}
h1{font-size:17px;letter-spacing:.14em;text-transform:uppercase;font-weight:700}
h1 b{color:var(--acc)}
.pill{display:inline-flex;align-items:center;gap:7px;font-size:12px;font-weight:600;padding:5px 10px;border-radius:999px;background:var(--card);border:1px solid var(--line);color:var(--mut);white-space:nowrap}
.pill i{width:8px;height:8px;border-radius:50%;background:var(--mut)}
.pill.ok{color:var(--ok);border-color:#1f4a33}.pill.ok i{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.pill.off{color:var(--bad);border-color:#5a2222}.pill.off i{background:var(--bad)}
.strip{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:12px}
.strip div{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:9px 10px;min-width:0}
.strip small{display:block;color:var(--mut);font-size:10.5px;letter-spacing:.1em;text-transform:uppercase}
.strip b{display:block;font-size:17px;font-variant-numeric:tabular-nums;margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.strip b.sm{font-size:13px;white-space:normal;line-height:1.25;font-weight:600}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px;position:relative;overflow:hidden;transition:border-color .3s}
.card.warn{border-color:var(--warn)}.card.bad{border-color:var(--bad)}
.card .t{display:flex;justify-content:space-between;align-items:baseline;color:var(--mut);font-size:11px;letter-spacing:.12em;text-transform:uppercase;font-weight:600}
.card .t u{text-decoration:none;font-size:11px;color:var(--mut);letter-spacing:0;text-transform:none}
.card .k{color:var(--acc);font-size:10px;letter-spacing:.12em;text-transform:uppercase;font-weight:700;margin-top:8px}
.card .v{font-size:clamp(30px,9vw,42px);font-weight:700;line-height:1.05;margin:2px 0 6px;font-variant-numeric:tabular-nums;letter-spacing:-.02em}
.card.warn .v{color:var(--warn)}.card.bad .v{color:var(--bad)}
.card .s{font-size:12px;color:var(--mut);font-variant-numeric:tabular-nums}
.card .s b{color:var(--tx);font-weight:600}
.empty{display:none;text-align:center;color:var(--mut);padding:40px 16px;line-height:1.6}
.empty b{color:var(--tx);display:block;font-size:16px;margin-bottom:6px}
body.none .grid{display:none}body.none .empty{display:block}
.dtc{margin:14px 0 4px}
.dtc h2{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--mut);margin-bottom:8px}
.dtc h2 span{font-weight:600}
.mil{font-size:11px;font-weight:700;padding:3px 9px;border-radius:999px;letter-spacing:.06em;text-transform:none}
.mil.on{background:#3a1515;color:var(--bad);border:1px solid #5a2222}.mil.off{background:#12301f;color:var(--ok);border:1px solid #1f4a33}
.dtc .note{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px;color:var(--mut);font-size:13px;line-height:1.5}
.dtc .note.ok{color:var(--ok);border-color:#1f4a33}
.grp{margin-top:10px}
.grp h3{font-size:11px;color:var(--mut);letter-spacing:.1em;text-transform:uppercase;margin:0 0 6px 2px}
.grp h3 b{color:var(--tx)}
.code{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 12px;margin-bottom:6px;display:grid;grid-template-columns:auto 1fr;gap:2px 12px;align-items:baseline}
.code.st{border-color:#5a2222}.code.pd{border-color:#5a4a1a}
.code .c{font-size:20px;font-weight:700;letter-spacing:.02em;font-variant-numeric:tabular-nums;grid-row:span 2}
.code .s{font-size:13px;font-weight:600}
.code .d{font-size:12px;color:var(--mut)}
.ff{background:#101418;border:1px dashed var(--line);border-radius:10px;padding:8px 10px;margin:2px 0 8px;font-size:12px;color:var(--mut)}
.ff b{color:var(--tx);font-weight:600}
.ff .t{display:block;font-size:10.5px;letter-spacing:.1em;text-transform:uppercase;margin-bottom:4px}
.ff .chips{display:flex;flex-wrap:wrap;gap:6px 14px}
footer{margin-top:16px;color:var(--mut);font-size:11.5px;text-align:center;line-height:1.6}
footer code{color:var(--tx)}
@media (min-width:700px){body{padding:20px;max-width:1100px;margin:0 auto}.grid{grid-template-columns:repeat(auto-fill,minmax(200px,1fr))}.strip b{font-size:20px}}
</style>
</head>
<body>
<header>
  <h1>Track <b>Metrics</b></h1>
  <span class="pill" id="pill"><i></i><span id="pillt">conectando…</span></span>
</header>

<section class="strip">
  <div><small>Sessão</small><b id="ses">--:--</b></div>
  <div><small>Amostras</small><b id="smp">0</b></div>
  <div><small>Estado</small><b id="st" class="sm">—</b></div>
</section>

<section class="grid" id="grid"></section>
<section class="empty"><b>Nenhuma sessão registrada</b>Conecte o ELM327 pela tela <em>blelist</em>, rode, e volte aqui: o resumo da última sessão fica guardado no painel.</section>

<section class="dtc" id="dtc">
  <h2><span>Códigos de falha</span><span id="dtcmeta"></span></h2>
  <div id="dtcbody"><div class="note">aguardando o painel…</div></div>
</section>

<footer>CarDash · <code>http://192.168.4.1</code><br>Enquanto esta página está no ar, o Bluetooth do painel fica pausado. Saia da tela do QR no painel para religá-lo.</footer>

<script>
/* Cada card destaca o valor que importa num track day: o pico, ou o piso
   quando é ele que assusta (bateria afundando, lambda pobre). th = limites
   de alerta para o valor destacado: [warnLow, warnHigh, badLow, badHigh]. */
const CARDS=[
  {k:"boost", n:"Boost",   u:"bar", d:2, big:"max",                 th:[null,null,null,null]},
  {k:"lambda",n:"Lambda",  u:"λ",   d:2, big:"min", other:"max",    th:[0.80,null,0.75,null]},
  {k:"water", n:"Água",    u:"°C",  d:0, big:"max",                 th:[null,105,null,112]},
  {k:"oil",   n:"Óleo",    u:"°C",  d:0, big:"max",                 th:[null,120,null,130]},
  {k:"iat",   n:"Admissão",u:"°C",  d:0, big:"max",                 th:[null,50,null,60]},
  {k:"batt",  n:"Bateria", u:"V",   d:1, big:"min", other:"max",    th:[12.5,null,12.0,null]},
  {k:"rail",  n:"Rail",    u:"bar", d:1, big:"max",                 th:[null,null,null,null]},
];
const POLL=3000;
const grid=document.getElementById("grid"), els={};
const LBL={max:"máximo",min:"mínimo"};

for(const c of CARDS){
  const card=document.createElement("article"); card.className="card";
  card.innerHTML=`<div class="t"><span>${c.n}</span><u>${c.u}</u></div>
    <div class="k">${LBL[c.big]}</div><div class="v" data-v>—</div>
    ${c.other?`<div class="s">${LBL[c.other]} <b data-o>—</b></div>`:''}`;
  grid.appendChild(card);
  els[c.k]={card, v:card.querySelector("[data-v]"), o:card.querySelector("[data-o]")};
}

const fmt=(v,d)=>(v==null||isNaN(v))?"—":Number(v).toFixed(d).replace(".",",");
const pad=n=>String(n).padStart(2,"0");
const dur=s=>{s=Math.max(0,s|0);const h=(s/3600)|0,m=((s%3600)/60)|0,x=s%60;return h?`${h}:${pad(m)}:${pad(x)}`:`${pad(m)}:${pad(x)}`};
const ago=s=>s<60?"há "+s+" s":s<3600?"há "+((s/60)|0)+" min":"há "+((s/3600)|0)+" h";

function level(v,th){
  if(v==null||!th) return "";
  const [wl,wh,bl,bh]=th;
  if((bl!=null&&v<bl)||(bh!=null&&v>bh)) return "bad";
  if((wl!=null&&v<wl)||(wh!=null&&v>wh)) return "warn";
  return "";
}

const pill=document.getElementById("pill"), pillt=document.getElementById("pillt");
let fails=0;
function setPill(state,text){pill.className="pill "+state; pillt.textContent=text}

function render(d){
  document.body.classList.toggle("none", !d.session);
  setPill("ok", d.active ? "sessão em andamento" : "painel conectado");
  document.getElementById("ses").textContent = d.session ? dur(d.session_s) : "--:--";
  document.getElementById("smp").textContent = (d.samples||0).toLocaleString("pt-BR");
  document.getElementById("st").textContent = !d.session ? "sem sessão" : d.active ? "ao vivo" : "encerrada "+ago(d.ended_s_ago);

  for(const c of CARDS){
    const e=els[c.k], big=d[c.big]?.[c.k], other=c.other?d[c.other]?.[c.k]:null;
    e.v.textContent = d.session ? fmt(big,c.d) : "—";
    if(e.o) e.o.textContent = d.session ? fmt(other,c.d) : "—";
    e.card.classList.remove("warn","bad");
    const lv = d.session ? level(big,c.th) : ""; if(lv) e.card.classList.add(lv);
  }
}

async function tick(){
  const ctl=new AbortController(); const t=setTimeout(()=>ctl.abort(),2500);
  try{
    const r=await fetch("/data",{cache:"no-store",signal:ctl.signal});
    if(!r.ok) throw new Error(r.status);
    render(await r.json()); fails=0;
  }catch(e){
    if(++fails>=2) setPill("off","painel offline");
  }finally{ clearTimeout(t); }
}
/* ---- DTCs ---------------------------------------------------------------
   Decodificacao estrutural SAE J2012, sem tabela: sistema (letra), origem
   (2o digito) e subsistema (3o digito, para codigos de powertrain). */
const SYS={P:"Powertrain — motor e transmissão",C:"Chassi — freios, direção, suspensão",B:"Carroceria — conforto e segurança",U:"Rede — comunicação entre módulos"};
const PSUB={"0":"Mistura ar/combustível e emissões auxiliares","1":"Dosagem de ar/combustível","2":"Dosagem de ar/combustível — circuito dos injetores","3":"Ignição e falha de combustão","4":"Controle auxiliar de emissões (EGR, catalisador, EVAP…)","5":"Velocidade, marcha lenta e entradas auxiliares","6":"Módulo de controle e circuitos de saída","7":"Transmissão","8":"Transmissão","9":"Transmissão","A":"Propulsão híbrida","B":"Propulsão híbrida","C":"Propulsão híbrida"};
function dtcInfo(code){
  const sys=code[0], d1=code[1], d2=code[2], tail=code.slice(3);
  const generic = d1==="0"||d1==="2"||(d1==="3"&&d2>="4");
  let sub = generic && sys==="P" ? (PSUB[d2]||"") : "";
  if(sys==="P"&&d1==="0"&&d2==="3"){ const n=parseInt(tail,10); if(n===0) sub+=" · falha aleatória / múltiplos cilindros"; else if(n>=1&&n<=12) sub+=" · cilindro "+n; }
  return {sys:SYS[sys]||sys, origin:generic?"genérico SAE":"específico do fabricante", sub};
}
const el=(t,c,h)=>{const e=document.createElement(t); if(c) e.className=c; if(h!=null) e.innerHTML=h; return e};
function codeCard(code,cls){
  const i=dtcInfo(code);
  return el("div","code "+cls,`<div class="c">${code}</div><div class="s">${i.sys}</div><div class="d">${i.origin}${i.sub?" · "+i.sub:""}</div>`);
}
function renderDtc(d){
  const body=document.getElementById("dtcbody"), meta=document.getElementById("dtcmeta");
  body.innerHTML="";
  if(!d.valid){ meta.textContent=""; body.appendChild(el("div","note","Ainda não lido. Os códigos são lidos pelo ELM327 ao conectar e a cada 60 s; conecte o carro pela tela <em>blelist</em> e volte aqui.")); return; }
  meta.innerHTML=`<span class="mil ${d.mil?"on":"off"}">luz da injeção ${d.mil?"ACESA":"apagada"}</span>`;
  const total=d.stored.length+d.pending.length+d.permanent.length;
  const age=el("div","note"+(total?"":" ok"), total?`${total} código${total>1?"s":""} · lido ${ago(d.age_s)}`:`Nenhum código de falha · lido ${ago(d.age_s)}`);
  body.appendChild(age);
  const groups=[["Armazenados",d.stored,"st","confirmados pela ECU; acendem a luz"],["Pendentes",d.pending,"pd","vistos uma vez; confirmam se repetirem"],["Permanentes",d.permanent,"pm","só o carro apaga, após ciclos sem a falha"]];
  for(const [name,list,cls,hint] of groups){
    if(!list.length) continue;
    const g=el("div","grp",`<h3><b>${name}</b> · ${list.length} · ${hint}</h3>`);
    for(const c of list) g.appendChild(codeCard(c,cls));
    if(cls==="st"&&d.ff){
      const f=d.ff;
      g.appendChild(el("div","ff",`<span class="t">Freeze frame · momento em que ${f.dtc} gravou</span><div class="chips"><span><b>${f.rpm}</b> rpm</span><span><b>${f.speed}</b> km/h</span><span>água <b>${f.coolant}</b> °C</span><span>carga <b>${f.load}</b>%</span><span>borboleta <b>${f.throttle}</b>%</span><span>MAP <b>${f.map}</b> kPa</span></div>`));
    }
    body.appendChild(g);
  }
}
async function tickDtc(){
  try{ const r=await fetch("/dtc",{cache:"no-store"}); if(r.ok) renderDtc(await r.json()); }catch(e){}
}

tick(); setInterval(tick,POLL);
tickDtc(); setInterval(tickDtc,POLL*3);
</script>
</body>
</html>
)html";

#endif
