'use strict';
(() => {
  const root = document.body;
  const id = root.dataset.cardId;
  const editable = root.dataset.mode === 'edit';
  const embedded = new URLSearchParams(location.search).get('embedded') === '1';
  if(embedded)root.classList.add('embedded');
  function notify(type, extra={}) { if(window.chrome && window.chrome.webview) window.chrome.webview.postMessage({type,...extra}); }

  const status = document.getElementById('state');
  const saveButton = document.getElementById('save');
  const sheetSelect = document.getElementById('sheet-select');
  let cardInfo=null;
  function renderIdentity(){if(cardInfo)document.getElementById('identity').textContent=`Тип платформы: ${cardInfo.platform_name} · Unique ID: ${cardInfo.id} · 1C Code: ${cardInfo.one_c_code || '—'}`;}
  let spreadsheet, revision = 0, savedSnapshot = '', dirty = false, saving = false, loaded = false;
  const adaptedSheets = new WeakSet();
  const clone = value => JSON.parse(JSON.stringify(value));
  function message(value, kind = '') { status.textContent = value; status.className = kind; }
  async function request(path, options) {
    const abort=new AbortController();const timeout=setTimeout(()=>abort.abort(),20000);let response;
    try{response=await fetch(path,{cache:'no-store',...options,signal:abort.signal});}
    catch(error){if(error.name==='AbortError')throw new TypeError('Сервер не ответил за 20 секунд.');throw error;}
    finally{clearTimeout(timeout);}
    let data;
    try { data = await response.json(); } catch { throw new Error('Сервер вернул некорректный ответ.'); }
    if (!response.ok) {
      if (response.status === 409) throw new Error('Бланк уже изменен в другом окне. Ваши правки оставлены в редакторе. Сначала проверьте сохраненную версию; автоматической перезаписи нет.');
      throw new Error(data.error || `Ошибка сервера: ${response.status}`);
    }
    return data;
  }
  function address(row, col) {
    let letters = ''; let value = col + 1;
    while (value) { value--; letters = String.fromCharCode(65 + value % 26) + letters; value = Math.floor(value / 26); }
    return letters + (row + 1);
  }
  function point(ref) {
    const match = /^([A-Z]+)([1-9][0-9]*)$/.exec(ref);
    if (!match) throw new Error('Некорректный диапазон ячеек.');
    let col = 0; for (const ch of match[1]) col = col * 26 + ch.charCodeAt(0) - 64;
    return [Number(match[2]) - 1, col - 1];
  }
  const theme = ['FFFFFF','000000','E7E6E6','44546A','4472C4','ED7D31','A5A5A5','FFC000','5B9BD5','70AD47','0563C1','954F72'];
  function color(value, fallback = '#000000') {
    if (!value) return fallback;
    let result = value.argb ? value.argb.slice(-6) : (value.theme !== undefined ? theme[value.theme] : null);
    if (!result || !/^[0-9A-Fa-f]{6}$/.test(result)) return fallback;
    if (value.tint) {
      const tint = value.tint;
      result = result.match(/../g).map(hex => {
        const n = parseInt(hex, 16);
        return Math.round(Math.max(0, Math.min(255, tint < 0 ? n * (1 + tint) : n * (1 - tint) + 255 * tint))).toString(16).padStart(2, '0');
      }).join('');
    }
    return '#' + result;
  }
  function styleFromExcel(cell) {
    const f = cell.font || {}, a = cell.alignment || {}, out = {
      font:{name:f.name || 'Calibri', size:f.size || 11, bold:!!f.bold, italic:!!f.italic},
      color:color(f.color), underline:!!f.underline, strike:!!f.strike,
      align:a.horizontal === 'centerContinuous' ? 'center' : (a.horizontal || 'left'),
      valign:a.vertical === 'center' ? 'middle' : (a.vertical || 'bottom'), textwrap:!!a.wrapText,
      bgcolor:cell.fill && cell.fill.type === 'pattern' && cell.fill.pattern !== 'none' ? color(cell.fill.fgColor, '#ffffff') : '#ffffff'
    };
    if (!['left','center','right'].includes(out.align)) out.align = 'left';
    if (!['top','middle','bottom'].includes(out.valign)) out.valign = 'bottom';
    const borders = {};
    for (const edge of ['left','right','top','bottom']) {
      const b = (cell.border || {})[edge];
      if (b && b.style) borders[edge] = [b.style.toLowerCase().includes('dash') ? 'dashed' : b.style === 'hair' ? 'thin' : b.style, color(b.color)];
    }
    if (Object.keys(borders).length) out.border = borders;
    return out;
  }
  function cellText(cell) {
    const value = cell.value;
    if (value === null || value === undefined) return '';
    if (value instanceof Date) return value.toLocaleDateString('ru-RU');
    if (typeof value === 'object') {
      if (value.formula) return '=' + value.formula;
      if (value.richText) return value.richText.map(part => part.text).join('');
      if (value.text !== undefined) return String(value.text);
      if (value.error) return value.error;
    }
    return String(value);
  }
  function sheetFromExcel(ws, card, fillIdentity) {
    const rowCount = Math.max(ws.rowCount, 40), colCount = Math.max(ws.columnCount, 16);
    if (rowCount > 20000 || colCount > 400) throw new Error('Шаблон превышает поддерживаемый размер таблицы.');
    const sheet = {name:ws.name || 'Карточка', freeze:'A1', styles:[], merges:[], rows:{len:rowCount}, cols:{len:colCount}};
    const styles = new Map();
    function styleIndex(style) {
      const key = JSON.stringify(style);
      if (!styles.has(key)) { styles.set(key, sheet.styles.length); sheet.styles.push(style); }
      return styles.get(key);
    }
    for (let c = 1; c <= ws.columnCount; c++) {
      const col = ws.getColumn(c);
      sheet.cols[c - 1] = {width:Math.max(20, Math.round((col.width || 8.43) * 7 + 5))};
    }
    for (let r = 1; r <= ws.rowCount; r++) {
      const row = ws.getRow(r), out = {height:Math.round((row.height || 15) * 4 / 3), cells:{}};
      for (let c = 1; c <= ws.columnCount; c++) {
        const cell = row.getCell(c);
        if (cell.isMerged && cell.master.address !== cell.address) continue;
        let value = cellText(cell);
        if (fillIdentity && /^\s*Код 1[СC]\s*:/.test(value)) value = 'Код 1С: ' + (card.one_c_code || '');
        if (fillIdentity && /^#ID/.test(value.trim())) value = '#ID ' + card.product_id;
        if (fillIdentity && /^\s*Наименование товара:/.test(value)) value = 'Наименование товара: ' + card.platform_name;
        if (value !== '' || Object.keys(cell.style || {}).length) out.cells[c - 1] = {text:value, style:styleIndex(styleFromExcel(cell)),...(/^\s*Код 1[СC]\s*:/.test(value)?{repairField:'one_c_code'}:{})};
      }
      sheet.rows[r - 1] = out;
    }
    function merge(ref) {
      const [start, end = start] = ref.split(':'); const [r,c] = point(start), [er,ec] = point(end);
      if (!sheet.rows[r]) sheet.rows[r] = {cells:{}};
      const cell = sheet.rows[r].cells[c] || {text:''};
      const s = clone(sheet.styles[cell.style] || {});
      const last = ws.getCell(er + 1, ec + 1);
      const b = styleFromExcel(last).border || {};
      s.border = {...(s.border || {})};
      if (b.right) s.border.right = b.right;
      if (b.bottom) s.border.bottom = b.bottom;
      cell.style = styleIndex(s); cell.merge = [er-r, ec-c];
      sheet.rows[r].cells[c] = cell; sheet.merges.push(ref);
      for (let rr=r; rr<=er; rr++) for (let cc=c; cc<=ec; cc++)
        if (rr !== r || cc !== c) { if (sheet.rows[rr]) delete sheet.rows[rr].cells[cc]; }
    }
    for (const ref of ws.model.merges || []) merge(ref);
    // Excel's "center across selection" is represented by a merged span in the web editor.
    for (let r=1; r<=ws.rowCount; r++) for (let c=1; c<=ws.columnCount; c++) {
      const cell = ws.getCell(r,c);
      if ((cell.alignment || {}).horizontal !== 'centerContinuous' || !cellText(cell) || cell.isMerged) continue;
      let last = c;
      while (last < ws.columnCount) {
        const next = ws.getCell(r,last+1);
        if (cellText(next) || next.isMerged || (next.alignment || {}).horizontal !== 'centerContinuous') break;
        last++;
      }
      if (last > c) merge(address(r-1,c-1) + ':' + address(r-1,last-1));
    }
    return sheet;
  }
  function toExcelStyle(style = {}) {
    const f = style.font || {}, b = {};
    for (const edge of ['left','right','top','bottom']) if (style.border && style.border[edge]) {
      const [kind, value] = style.border[edge];
      b[edge] = {style:kind === 'dashed' ? 'dashed' : kind, color:{argb:'FF' + String(value || '#000000').replace('#','')}};
    }
    return {
      font:{name:f.name || 'Calibri',size:f.size || 11,bold:!!f.bold,italic:!!f.italic,underline:!!style.underline,strike:!!style.strike,color:{argb:'FF'+String(style.color || '#000000').replace('#','')}},
      alignment:{horizontal:style.align || 'left',vertical:style.valign === 'middle' ? 'middle' : (style.valign || 'bottom'),wrapText:!!style.textwrap},
      fill:{type:'pattern',pattern:'solid',fgColor:{argb:'FF'+String(style.bgcolor || '#ffffff').replace('#','')}}, border:b
    };
  }
  async function workbookBytes(documentState) {
    const book = new ExcelJS.Workbook(); book.creator = 'Repair Cards';
    for (const data of documentState.sheets) {
      const ws = book.addWorksheet(data.name || 'Карточка');
      for (const [key, col] of Object.entries(data.cols)) if (key !== 'len') {
        const target = ws.getColumn(Number(key)+1);
        if (col.width !== undefined) target.width = Math.max(.1,(col.width-5)/7);
        target.hidden = !!col.hide;
      }
      for (const [key, row] of Object.entries(data.rows)) if (key !== 'len') {
        const target = ws.getRow(Number(key)+1);
        if (row.height !== undefined) target.height = row.height*3/4;
        target.hidden = !!row.hide;
        for (const [c, cell] of Object.entries(row.cells || {})) {
          const out = target.getCell(Number(c)+1), value = cell.text === undefined || cell.text === null ? '' : String(cell.text);
          out.value = value.startsWith('=') ? {formula:value.slice(1)} : value;
          if(cell.repairField==='one_c_code')book.definedNames.add("'"+ws.name.replace(/'/g,"''")+"'!$"+address(Number(key),Number(c)).replace(/(\d+)$/, '$$$1'), 'RepairCards_OneCCode');
          out.style = toExcelStyle((data.styles || [])[cell.style]);
        }
      }
      for (const ref of data.merges || []) ws.mergeCells(ref);
    }
    return new Uint8Array(await book.xlsx.writeBuffer());
  }
  function base64(bytes) {
    let result = ''; const chunk = 0x8000;
    for (let i=0; i<bytes.length; i+=chunk) result += String.fromCharCode(...bytes.subarray(i,i+chunk));
    return btoa(result);
  }
  function snapshot() { return JSON.stringify({format:'x-data-spreadsheet',version:1,sheets:spreadsheet.getData()}); }
  function prepareSheets() {
    for (const data of spreadsheet.datas) {
      if (adaptedSheets.has(data)) continue;
      adaptedSheets.add(data);
      // Keep custom column widths with their columns during insert/delete and undo.
      const insert = data.rows.insertColumn.bind(data.rows);
      data.rows.insertColumn = (at, n = 1) => {
        insert(at,n);
        const shifted = {};
        for (const [key,value] of Object.entries(data.cols._)) shifted[Number(key)>=at ? Number(key)+n : key] = value;
        data.cols._ = shifted;
      };
      const remove = data.rows.deleteColumn.bind(data.rows);
      data.rows.deleteColumn = (first,last) => {
        remove(first,last);
        const shifted = {}, n=last-first+1;
        for (const [key,value] of Object.entries(data.cols._)) {
          const index=Number(key);
          if (index<first) shifted[index]=value;
          else if (index>last) shifted[index-n]=value;
        }
        data.cols._ = shifted;
      };
    }
  }
  function checkDirty() {
    if (!editable || !loaded || saving) return;
    prepareSheets();
    const changed = snapshot() !== savedSnapshot;
    if (changed && !dirty) message('Есть несохраненные изменения. Нажмите «Сохранить».', 'dirty');
    dirty = changed; saveButton.disabled = !dirty; notify('state',{dirty,saving});
  }
  async function save() {
    if (!editable || saving || !loaded) return;
    // Commit the cell editor before taking an immutable snapshot.
    const editor = spreadsheet.sheet.editor;
    if (editor) editor.clear();
    const state = snapshot(), documentState = JSON.parse(state);
    saving = true; saveButton.disabled = true; message('Сохраняем бланк…');
    try {
      const bytes = await workbookBytes(documentState);
      const result = await request(`/api/cards/${id}/document`, {method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision,document:documentState,workbook_base64:base64(bytes)})});
      revision = result.revision; savedSnapshot = state; if(cardInfo && typeof result.one_c_code==='string'){cardInfo.one_c_code=result.one_c_code;renderIdentity();}
      dirty = snapshot() !== state;
      message(dirty ? 'Бланк сохранен. Есть новые несохраненные правки.' : 'Бланк сохранен.', dirty ? 'dirty' : '');
    } catch (error) {
      dirty = true;
      message(error instanceof TypeError ? 'Нет ответа сервера. Правки остаются в этом окне. Сохранение могло завершиться — проверьте сохраненную версию перед повтором.' : error.message, 'error');
    } finally { saving = false; saveButton.disabled = !dirty; notify('state',{dirty,saving}); }
    return !dirty;
  }
  async function start() {
    try {
      const [card, stored] = await Promise.all([request(`/api/cards/${id}`), request(`/api/cards/${id}/document`)]);
      if(typeof stored.one_c_code==='string')card.one_c_code=stored.one_c_code;
      document.getElementById('title').textContent = (editable ? 'Редактирование: ' : 'Просмотр: ') + card.product_id;
      cardInfo=card;renderIdentity();
      let state = stored.document; revision = stored.revision;
      if (!state) {
        message('Загружаем Excel-шаблон…');
        const response = await fetch(`/api/cards/${id}/workbook`, {cache:'no-store'});
        if (!response.ok) throw new Error('Не удалось загрузить Excel-бланк.');
        const book = new ExcelJS.Workbook(); await book.xlsx.load(await response.arrayBuffer());
        state = {format:'x-data-spreadsheet',version:1,sheets:book.worksheets.map(ws=>sheetFromExcel(ws,card,true))};
      }
      if (!state.sheets.length) throw new Error('В бланке нет листов.');
      const holder = document.getElementById('spreadsheet');
      spreadsheet = window.x_spreadsheet(holder, {
        mode:editable ? 'edit' : 'read',showToolbar:editable,showContextmenu:editable,showBottomBar:editable,
        showGrid:editable,
        view:{height:()=>Math.max(280,window.innerHeight-holder.getBoundingClientRect().top-34),width:()=>window.innerWidth},
        row:{len:40,height:20},col:{len:16,width:64,indexWidth:42,minWidth:20},
        style:{font:{name:'Calibri',size:11},valign:'bottom'}
      }).loadData(state.sheets);
      // The vendor's invisible sizing element must treat entered text as text, not HTML.
      const textline = spreadsheet.sheet.editor.textlineEl;
      textline.html = function(value) {
        if (value !== undefined) { this.el.textContent = value; return this; }
        return this.el.textContent;
      };
      window.repairSheet = spreadsheet;
      if (editable) prepareSheets();
      if (editable) setTimeout(() => {
        const toolbar = spreadsheet.sheet.toolbar;
        toolbar.btns2 = toolbar.btns2.filter(([element]) => {
          if (!element.el.querySelector('.x-spreadsheet-icon-img.print')) return true;
          element.el.remove(); return false;
        });
        window.dispatchEvent(new Event('resize'));
      }, 0);
      if (!editable) {
        sheetSelect.replaceChildren();
        state.sheets.forEach((sheet,index)=>{const option=document.createElement('option');option.value=String(index);option.textContent=sheet.name;sheetSelect.appendChild(option);});
        sheetSelect.addEventListener('change',()=>spreadsheet.sheet.resetData(spreadsheet.datas[Number(sheetSelect.value)]));
      } else sheetSelect.parentElement.hidden = true;
      savedSnapshot = snapshot(); loaded = true; notify('state',{dirty:false,saving:false});
      spreadsheet.change(checkDirty);
      spreadsheet.on('cell-edited', checkDirty);
      if(embedded){
        const data=state.sheets[0];let lastRow=0,lastCol=0;
        for(const [r,row] of Object.entries(data.rows))if(r!=='len')for(const [c,cell] of Object.entries(row.cells||{}))if(cell.text || cell.style!==undefined || cell.merge){lastRow=Math.max(lastRow,Number(r)+(cell.merge?.[0]||0));lastCol=Math.max(lastCol,Number(c)+(cell.merge?.[1]||0));}
        let width=70,height=editable?180:140;for(let c=0;c<=lastCol;c++)width+=(data.cols[c]?.width||64);for(let r=0;r<=lastRow;r++)height+=(data.rows[r]?.height||20);
        notify('ready',{title:card.product_id,mode:editable?'edit':'read',width:Math.ceil(width),height:Math.ceil(height)});
      }
      message(editable ? 'Бланк открыт. Правки сохраняются кнопкой «Сохранить».' : 'Бланк открыт только для просмотра.');
      if (editable) {
        setInterval(checkDirty, 500);
        document.getElementById('insert-row').addEventListener('click',()=>{
          const data=spreadsheet.sheet.data;
          if (data.rows.len >= 20000) return message('Достигнут предел 20 000 строк.', 'error');
          spreadsheet.sheet.editor.clear(); data.insert('row'); spreadsheet.sheet.resetData(data); checkDirty();
        });
        document.getElementById('insert-column').addEventListener('click',()=>{
          const data=spreadsheet.sheet.data;
          if (data.cols.len >= 400) return message('Достигнут предел 400 столбцов.', 'error');
          spreadsheet.sheet.editor.clear(); data.insert('column'); spreadsheet.sheet.resetData(data); checkDirty();
        });
      }
    } catch (error) { message('Не удалось открыть бланк: '+error.message, 'error'); }
  }
  saveButton.addEventListener('click',save);
  document.getElementById('reload').addEventListener('click',()=>{
    if (!dirty || confirm('Открыть сохраненную версию? Несохраненные правки в этом окне будут потеряны.')) { dirty=false; loaded=false; location.reload(); }
  });
  document.addEventListener('keydown',event=>{if(editable && (event.ctrlKey || event.metaKey) && event.key.toLowerCase()==='s'){event.preventDefault();event.stopPropagation();save();}},true);
  window.addEventListener('beforeunload',event=>{if(editable && loaded && (dirty || saving || snapshot()!==savedSnapshot)){event.preventDefault();event.returnValue='';}});
  window.repairIsDirty=()=>{if(!editable||!loaded)return false;if(spreadsheet.sheet.editor)spreadsheet.sheet.editor.clear();checkDirty();return dirty||saving||snapshot()!==savedSnapshot;};
  window.repairSaveAndClose=async()=>{ const ok=await save();notify('save-close',{ok:!!ok}); };
  document.addEventListener('input',()=>{if(editable&&loaded)notify('state',{dirty:true,saving});});
  start();
})();
