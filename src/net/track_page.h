// Pagina servida em http://192.168.4.1/ pelo track_server. Autocontida de
// proposito: o celular esta na rede do ESP, sem internet, entao nada de CDN,
// fonte externa ou biblioteca. Mostra o RESUMO da ultima sessao (max/min,
// duracao, amostras): enquanto o Wi-Fi esta no ar o Bluetooth fica suspenso,
// entao nao ha dado ao vivo para mostrar.
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
tick(); setInterval(tick,POLL);
</script>
</body>
</html>
)html";

#endif
