'use strict';
// The server uses the same three-way merge (src/merge.cpp). Later queued
// changes win a shared property. Row/column and merge-layout changes replace
// the affected sheet, rather than guessing where moved cells belong.
window.RepairMerge=(()=>{
  const clone=x=>JSON.parse(JSON.stringify(x));
  const object=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
  function stable(x){if(Array.isArray(x))return '['+x.map(stable).join(',')+']';if(object(x))return '{'+Object.keys(x).sort().map(k=>JSON.stringify(k)+':'+stable(x[k])).join(',')+'}';return JSON.stringify(x);}
  const equal=(a,b)=>stable(a)===stable(b);
  function merge(b,c,l){
    if(equal(l,b))return clone(c);
    if(equal(c,b)||!object(b)||!object(c)||!object(l))return clone(l);
    const out=clone(c);
    for(const key of new Set([...Object.keys(b),...Object.keys(l)])){
      if(!(key in l)){if(key in b)delete out[key];continue;}
      if(key in b&&equal(l[key],b[key]))continue;
      out[key]=key in c?merge(key in b?b[key]:{},c[key],l[key]):clone(l[key]);
    }
    return out;
  }
  function expand(sheet){sheet=clone(sheet);const styles=sheet.styles||[];delete sheet.styles;for(const row of Object.values(sheet.rows))for(const cell of Object.values(row.cells||{}))if(cell.style!==undefined){cell._expandedStyle=clone(styles[cell.style]);delete cell.style;}return sheet;}
  function compact(sheet){const styles=[];for(const row of Object.values(sheet.rows))for(const cell of Object.values(row.cells||{}))if(cell._expandedStyle!==undefined){let i=styles.findIndex(s=>equal(s,cell._expandedStyle));if(i<0){i=styles.length;styles.push(cell._expandedStyle);}cell.style=i;delete cell._expandedStyle;}sheet.styles=styles;return sheet;}
  const structure=(b,c)=>b.rows.len!==c.rows.len||b.cols.len!==c.cols.len||!equal(b.merges||[],c.merges||[]);
  function documents(b,c,l){
    if(equal(l,b))return clone(c);if(equal(c,b))return clone(l);
    if(b.sheets.length!==c.sheets.length||b.sheets.length!==l.sheets.length)return clone(l);
    const out=clone(l);out.sheets=l.sheets.map((sheet,i)=>{
      const base=b.sheets[i],current=c.sheets[i];
      if(equal(sheet,base))return clone(current);
      if(base.name!==current.name||base.name!==sheet.name||structure(base,current)||structure(base,sheet))return clone(sheet);
      return compact(merge(expand(base),expand(current),expand(sheet)));
    });return out;
  }
  return {documents,equal};
})();
