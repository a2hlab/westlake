
'use strict';
const audit=JSON.parse(document.getElementById('audit-data').textContent), apps=audit.apps, summary=audit.summary;
const byId=id=>document.getElementById(id);
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const metric=(v,label,note)=>`<div class="metric"><strong>${v}</strong>${esc(label)}<small>${esc(note)}</small></div>`;
byId('metrics').innerHTML=metric(apps.length,'Ranked apps','Every entry reconciled')+metric(summary.archive_scanned,'Fresh archive scans','18 archives unavailable')+metric(summary.latest_saved_launch.drawing,'Latest saved drawing','Historical; not full acceptance')+metric(978-summary.latest_saved_launch.drawing,'Below drawing','22 additional apps unscored')+metric(summary.post_cutoff_status9_apps,'Observed Google refusals','Targeted later evidence only');
byId('feature-table').querySelector('tbody').innerHTML=summary.features.map(x=>`<tr><td>${esc(x.feature)}</td><td>${x.static_type_presence_apps}<div class="bar"><span style="width:${x.static_type_presence_apps/982*100}%"></span></div></td><td>${x.l8f_requested_apps??'not measured'}</td><td>${x.post_cutoff_status9_apps??'not measured'}</td></tr>`).join('');
let page=0, filtered=apps;const pageSize=50;
const searchText=new Map(apps.map(a=>[a.package,[a.rank,a.title,a.package,a.stage,a.latest_score?.blocker,a.google_families.join(' '),a.post_cutoff_status9_names.join(' ')].join(' ').toLowerCase()]));
function applyFilters(){
 const q=byId('search').value.trim().toLowerCase(), st=byId('stage').value,g=byId('google').value,t=byId('testing').value;
 filtered=apps.filter(a=>(!q||searchText.get(a.package).includes(q))&&(!st||(st==='non-drawing'?a.stage!=='drawing'&&a.stage!=='unscored':a.stage===st))&&(!g||(g==='refused'?a.post_cutoff_status9_services.length>0:g==='post'?a.post_cutoff_log_count>0:g==='no-post'?a.post_cutoff_log_count===0:a.google_families.includes(g)))&&(!t||(t==='no-exploration'?!a.latest_exploration:a.test_level===t)));
 page=0;render();
}
function render(){
 const pages=Math.max(1,Math.ceil(filtered.length/pageSize));page=Math.min(page,pages-1);
 byId('result-count').textContent=`${filtered.length.toLocaleString()} of 1,000 apps match`;
 byId('page-count').textContent=`${page+1} / ${pages}`;byId('prev').disabled=page===0;byId('next').disabled=page+1>=pages;byId('empty').hidden=filtered.length!==0;
 byId('app-table').querySelector('tbody').innerHTML=filtered.slice(page*pageSize,(page+1)*pageSize).map(a=>`<tr><td><small>#${a.rank}</small><button type="button" class="app-link" data-package="${esc(a.package)}">${esc(a.title)}</button><small>${esc(a.package)}</small></td><td><span class="chip ${a.stage==='unscored'?'unknown':''}">${esc(a.stage)}</span><small>${esc(a.latest_score?.round??'No scored record')} ${esc(a.latest_score?.record_utc.slice(0,10)??'')}</small></td><td>${a.post_cutoff_status9_services.length?'<span class="chip warn">Observed refusal</span>':a.post_cutoff_log_count?'<span class="chip unknown">No identified refusal</span>':'<span class="chip unknown">No later evidence</span>'}<small>${esc(a.post_cutoff_status9_names.join(', ')||`${a.google_families.length} static families`)}</small></td><td>${esc(a.test_level)}<small>${a.scored_rounds} launch rounds${a.latest_exploration?'; '+esc(a.latest_exploration.status):''}</small></td><td><small>${esc(a.next_test)}</small></td></tr>`).join('');
}
function openApp(pkg){
 const a=apps.find(x=>x.package===pkg);if(!a)return;
 byId('detail-rank').textContent=`Rank ${a.rank} / ${a.stage} / ${a.test_level}`;byId('detail-title').textContent=a.title;
 let body=`<p><code>${esc(a.package)}</code><br><span class="subtle">Registry/scan version: ${esc(a.version??'unknown')}; not tied to old run versions.</span></p>`;
 const sections=[['Running / launch',a.running_description],['Observed blockers',a.blocked_description],['What was tested',a.tested_description],['Earlier Google API observations',a.earlier_api_observations],['Later Google API observations',a.post_cutoff_api_observations],['Remaining gaps',a.gaps_description],['Next test',a.next_test],['Fresh static SDK families',a.google_families.join(', ')||'No observed types / archive unavailable']];
 for(const [title,content] of sections)body+=`<section class="detail-section"><h3>${esc(title)}</h3><p>${esc(content)}</p></section>`;
 body+=`<details><summary>Historical build-120 candidates (${a.historical_gap_count})</summary><p>Stale baseline hypotheses, not current failures. Full rows are in historical-api-gaps.csv / apps.json.</p><p>${esc(Object.entries(a.historical_gap_categories).map(([k,v])=>k+': '+v).join('; '))}</p><ul>${a.historical_examples.map(x=>`<li><code>${esc(x.id)}</code> — ${esc(x.verdict)} (${esc(x.shim_class)})</li>`).join('')}</ul></details>`;
 const sources=[];if(a.latest_score)sources.push(`${a.latest_score.score_source}:${a.latest_score.score_line}`);if(a.latest_exploration)sources.push(a.latest_exploration.source);if(a.latest_functional)sources.push(a.latest_functional.source);
 for(const g of a.google_log_sources)sources.push(g.source+'; callback line examples: '+Object.entries(g.service_callback_lines).map(([k,v])=>k+' @ '+v.join(',')).join('; '));
 body+=`<details><summary>Evidence references</summary><p>Aliases resolve to the corpus and retained logs documented in README.md. Callback line examples may include both successful and failed callbacks.</p><ul class="source-list">${sources.map(x=>`<li><code>${esc(x)}</code></li>`).join('')||'<li>No runtime evidence</li>'}</ul></details>`;
 byId('detail-content').innerHTML=body;if(!byId('app-detail').open)byId('app-detail').showModal();byId('app-detail').scrollTop=0;
}
byId('app-table').addEventListener('click',e=>{const b=e.target.closest('[data-package]');if(b)openApp(b.dataset.package)});
byId('close-detail').addEventListener('click',()=>byId('app-detail').close());
for(const id of ['search','stage','google','testing'])byId(id).addEventListener(id==='search'?'input':'change',applyFilters);
byId('prev').addEventListener('click',()=>{page--;render()});byId('next').addEventListener('click',()=>{page++;render()});
render();
if(location.hash){try{openApp(decodeURIComponent(location.hash.slice(1)))}catch{}}
