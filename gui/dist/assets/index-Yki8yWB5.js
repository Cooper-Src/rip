(function(){const e=document.createElement("link").relList;if(e&&e.supports&&e.supports("modulepreload"))return;for(const i of document.querySelectorAll('link[rel="modulepreload"]'))o(i);new MutationObserver(i=>{for(const c of i)if(c.type==="childList")for(const m of c.addedNodes)m.tagName==="LINK"&&m.rel==="modulepreload"&&o(m)}).observe(document,{childList:!0,subtree:!0});function l(i){const c={};return i.integrity&&(c.integrity=i.integrity),i.referrerPolicy&&(c.referrerPolicy=i.referrerPolicy),i.crossOrigin==="use-credentials"?c.credentials="include":i.crossOrigin==="anonymous"?c.credentials="omit":c.credentials="same-origin",c}function o(i){if(i.ep)return;i.ep=!0;const c=l(i);fetch(i.href,c)}})();const Z=new URLSearchParams(window.location.search).get("native")==="1";document.body.classList.toggle("native-host",Z);let d="filesystem",F="C:\\",g="",f="",T=[],U=[],r=-1,q=!1;const J=document.querySelector("#app");if(!J)throw new Error("Unable to find #app.");const H=t=>t===null?"":t.toLocaleString("en-US").replaceAll(","," "),_=(t,e)=>t<=0?"0.0%":`${((t-e)/t*100).toFixed(1)}%`,N=t=>{switch(t){case"folder":return`
                <svg viewBox="0 0 24 24">
                    <path
                        d="M3 6.5h7l1.8 2H21v10.8H3z"
                        fill="#f4c542"
                        stroke="#c38d1f"
                        stroke-width=".7"
                    />
                    <path
                        d="M3 8.5h18"
                        stroke="#e0a92d"
                        stroke-width="1"
                    />
                </svg>
            `;case"vscode":return`
                <svg viewBox="0 0 24 24">
                    <path
                        d="M17.8 3.5 21 5.2v13.6l-3.2 1.7-8.6-6.7
                           -3.1 2.4-1.9-1.3 3-3.5-3-3.5 1.9-1.3
                           3.1 2.4z"
                        fill="#1687d9"
                    />
                    <path
                        d="m9.7 8.1 5.5-3v13.8l-5.5-3"
                        fill="#36a6ee"
                    />
                </svg>
            `;case"markdown":return`
                <svg viewBox="0 0 24 24">
                    <path
                        d="M5 2.5h9l5 5v14H5z"
                        fill="#f8f8f8"
                        stroke="#a7adb4"
                    />
                    <path
                        d="M14 2.5v5h5"
                        fill="none"
                        stroke="#a7adb4"
                    />
                    <rect
                        x="7"
                        y="13"
                        width="10"
                        height="4.5"
                        fill="#7b8189"
                    />
                    <text
                        x="8"
                        y="16.4"
                        font-size="3.5"
                        fill="white"
                        font-family="Segoe UI, sans-serif"
                        font-weight="700"
                    >
                        MD
                    </text>
                </svg>
            `;case"vlc":return`
                <svg viewBox="0 0 24 24">
                    <path
                        d="M12 2 6.1 20h11.8z"
                        fill="#e87521"
                    />
                    <path
                        d="M7.8 14.5h8.4M8.8 11.5h6.4"
                        stroke="white"
                        stroke-width="2"
                    />
                    <path
                        d="M8.5 20h7"
                        stroke="#d14f13"
                        stroke-width="1.5"
                    />
                </svg>
            `;case"browser":return`
                <svg viewBox="0 0 24 24">
                    <circle
                        cx="12"
                        cy="12"
                        r="9"
                        fill="#dcefff"
                        stroke="#73a9d8"
                    />
                    <path
                        d="M3.5 11.5h17M12 3c2.6 3.1 2.7 14.8 0 18"
                        stroke="#78a6cb"
                        stroke-width="1"
                        fill="none"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="2.4"
                        fill="#4b9ed8"
                    />
                </svg>
            `;case"exe":return`
                <svg viewBox="0 0 24 24">
                    <rect
                        x="3"
                        y="3"
                        width="18"
                        height="18"
                        fill="#f4f6f8"
                        stroke="#919aa3"
                    />
                    <rect
                        x="6"
                        y="6"
                        width="12"
                        height="8"
                        fill="#4d87c4"
                    />
                </svg>
            `;case"disc":return`
                <svg viewBox="0 0 24 24">
                    <path
                        d="M5 2.5h14v19H5z"
                        fill="#f7f7f7"
                        stroke="#a6adb5"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="5"
                        fill="#aeb5bc"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="1.8"
                        fill="#f4f4f4"
                    />
                </svg>
            `}};J.innerHTML=`
    <div class="app">

        <header class="titlebar">

            <div class="titlebar-left">

                <div class="rip-title-icon">
                    <span>RIP</span>
                </div>

                <div
                    id="window-path"
                    class="titlebar-path"
                >
                    C:\\
                </div>

            </div>

            <div class="window-buttons">
                <button class="window-button">
                    <span class="minimize-glyph"></span>
                </button>

                <button class="window-button">
                    <span class="maximize-glyph"></span>
                </button>

                <button class="window-button close">
                    <span class="close-glyph"></span>
                </button>
            </div>

        </header>

        <section class="toolbar">

            <button
                class="tool"
                data-action="add"
            >
                <span class="tool-icon add-icon">+</span>
                <span class="tool-label">Add</span>
            </button>

            <button
                class="tool"
                data-action="extract"
            >
                <span class="tool-icon extract-icon"></span>
                <span class="tool-label">Extract</span>
            </button>

            <button
                class="tool"
                data-action="test"
            >
                <span class="tool-icon test-icon"></span>
                <span class="tool-label">Test</span>
            </button>

            <button
                class="tool"
                data-action="delete"
            >
                <span class="tool-icon delete-icon">×</span>
                <span class="tool-label">Delete</span>
            </button>

            <button
                class="tool"
                data-action="info"
            >
                <span class="tool-icon info-icon">i</span>
                <span class="tool-label">Info</span>
            </button>

            <div class="toolbar-spacer"></div>

        </section>

        <section class="address-bar">

            <button
                id="up-button"
                class="up-button"
            >
                <span class="folder-arrow-icon">
                    ↑
                </span>
            </button>

            <div class="address-input">

                <span class="address-drive-icon">
                    ▼
                </span>

                <span
                    id="address-path"
                    class="address-path"
                >
                    C:\\
                </span>

                <span class="address-chevron">
                    
                </span>

            </div>

        </section>

        <main class="file-area">

            <div
                id="column-header"
                class="column-header"
            ></div>

            <div
                id="file-list"
                class="file-list"
            ></div>

        </main>

                <footer class="status-bar">

            <div
                id="status-text"
                class="status-text"
            >
                Loading...
            </div>

            <div class="status-pane"></div>
            <div class="status-pane"></div>
            <div class="status-pane"></div>

        </footer>

        <!-- New Archive Dialog -->

        <div
            id="new-archive-dialog"
            class="modal-backdrop hidden"
        >
            <div
                class="archive-dialog"
                role="dialog"
                aria-modal="true"
                aria-labelledby="archive-dialog-title"
            >

                <div class="dialog-titlebar">

                    <div>
                        <div
                            id="archive-dialog-title"
                            class="dialog-title"
                        >
                            Create RIP Archive
                        </div>

                        <div
                            id="dialog-source"
                            class="dialog-subtitle"
                        >
                            No source selected
                        </div>
                    </div>

                    <button
                        id="dialog-close"
                        class="dialog-close"
                        aria-label="Close"
                    >
                        ×
                    </button>

                </div>

                <div class="dialog-body">

                    <label class="dialog-field">
                        <span>Source</span>

                        <input
                            id="dialog-source-input"
                            type="text"
                            readonly
                        >
                    </label>

                    <label class="dialog-field">
                        <span>Compression preset</span>

                        <select id="dialog-preset">
                            <option value="deflate">
                                Deflate
                            </option>

                            <option
                                value="strong"
                                selected
                            >
                                Strong
                            </option>

                            <option value="maximum">
                                Maximum
                            </option>

                            <option value="ultra">
                                Ultra
                            </option>
                        </select>
                    </label>

                    <div class="preset-description">

                        <div
    id="preset-description-title"
    class="preset-description-title"
></div>

                        <div
                            id="preset-description"
                            class="preset-description-text"
                        >
                            DEFLATE now, with room for
                            additional codecs in the
                            expanded compression engine.
                        </div>

                    </div>

                    <div class="dialog-note">
    The archive will be created next to
    the selected file or folder.
</div>

                </div>

                <div class="dialog-footer">

                    <button
                        id="dialog-cancel"
                        class="dialog-button"
                    >
                        Cancel
                    </button>

                    <button
                        id="dialog-create"
                        class="dialog-button primary"
                    >
                        Create
                    </button>

                </div>

            </div>
        </div>

    </div>

    <div
    id="archive-progress-dialog"
    class="modal-backdrop hidden"
>
    <div
        class="archive-progress-dialog"
        role="dialog"
        aria-modal="true"
    >

        <div class="dialog-titlebar">

            <div>
                <div class="dialog-title">
                    Creating RIP Archive
                </div>

                <div
                    id="archive-progress-source"
                    class="dialog-subtitle"
                >
                    Preparing...
                </div>
            </div>

        </div>

        <div class="progress-body">

            <div class="progress-file-label">
                Current file
            </div>

            <div
                id="archive-progress-file"
                class="progress-file"
            >
                Preparing archive...
            </div>

            <div class="progress-method-row">

                <span>Stage</span>

                <strong id="archive-progress-stage">
                    Preparing
                </strong>

            </div>

            <div class="progress-track">
                <div
                    id="archive-progress-bar"
                    class="progress-bar"
                ></div>
            </div>

            <div class="progress-meta">

                <span
                    id="archive-progress-count"
                >
                    0 / 0 files
                </span>

                <strong
                    id="archive-progress-percent"
                >
                    0%
                </strong>

            </div>

        </div>

        <div class="progress-status-bar">

            <span
                id="archive-progress-status"
            >
                Preparing archive...
            </span>

        </div>

    </div>
</div>
`;const x=window.chrome?.webview,u=t=>{x&&x.postMessage(t)},j=document.querySelector("#status-text"),w=document.querySelector("#address-path"),S=document.querySelector("#window-path"),D=document.querySelector("#column-header"),p=document.querySelector("#file-list"),v=t=>{j&&(j.textContent=t)},K=t=>{if(D){if(!t){D.innerHTML=`
            <div class="column name-column">Name</div>
            <div class="column size-column">Size</div>
            <div class="column date-column">Modified</div>
            <div class="column date-column">Created</div>
            <div class="column">Comment</div>
            <div class="column size-column">Folders</div>
            <div class="column size-column">Files</div>
            <div class="column rest-column"></div>
        `;return}D.innerHTML=`
        <div class="column name-column">Name</div>
        <div class="column size-column">Original</div>
        <div class="column size-column">Packed</div>
        <div class="column size-column">Saved</div>
        <div class="column">Method</div>
        <div class="column"></div>
        <div class="column"></div>
        <div class="column rest-column"></div>
    `}},C=()=>{const t=d==="filesystem"&&r>=0?T[r]:void 0,e=t!==void 0&&t.kind==="file"&&t.name.toLowerCase().endsWith(".rip"),l=d==="archive"||e,o={add:d==="filesystem"&&r>=0&&!e,extract:l,test:l,delete:d==="filesystem"&&r>=0,info:l};document.querySelectorAll("[data-action]").forEach(i=>{const c=i.dataset.action;i.disabled=!o[c]})},L=document.querySelector("#new-archive-dialog"),B=document.querySelector("#dialog-source"),O=document.querySelector("#dialog-source-input"),h=document.querySelector("#dialog-preset"),$=document.querySelector("#preset-description"),P=document.querySelector("#preset-description-title"),G={deflate:{title:"Deflate",description:"Use DEFLATE compression for every compressible file."},strong:{title:"Strong",description:"Compare DEFLATE with additional codecs as the multi-codec engine expands."},maximum:{title:"Maximum",description:"Use several compression algorithms and keep the smallest result."},ultra:{title:"Ultra",description:"Spend significantly more CPU time searching for the smallest possible archive."},custom:{title:"Custom",description:"Choose which compression algorithms and levels RIP should use."}},ee=()=>{if(d!=="filesystem"||r<0){v("Select a file or folder first");return}const t=T[r];if(!t)return;B&&(B.textContent=t.name),O&&(O.value=t.path),h&&(h.value="strong");const e=G.strong;h&&(h.value="strong"),P&&(P.textContent=e.title),$&&($.textContent=e.description),L?.classList.remove("hidden"),h?.focus()},W=()=>{L?.classList.add("hidden")};h?.addEventListener("change",()=>{const t=h.value,e=G[t];e&&(P&&(P.textContent=e.title),$&&($.textContent=e.description))});document.querySelector("#dialog-close")?.addEventListener("click",W);document.querySelector("#dialog-cancel")?.addEventListener("click",W);document.querySelector("#dialog-create")?.addEventListener("click",()=>{if(d!=="filesystem"||r<0)return;const t=h?.value??"strong";u(`preset	${t}`),W(),Q(),u("action	add")});L?.addEventListener("click",t=>{t.target===L&&W()});const Q=()=>{V?.classList.remove("hidden"),q&&(q.style.width="0%"),A&&(A.textContent="0%"),E&&(E.textContent="0 / 0 files"),z&&(z.textContent="Preparing"),k&&(k.textContent="Preparing archive..."),M&&(M.textContent="Preparing archive...")},te=()=>{V?.classList.add("hidden")},se=t=>{Q(),R&&(R.textContent=t.file),k&&(k.textContent=t.file),z&&(z.textContent=t.stage),q&&(q.style.width=`${t.percent}%`),A&&(A.textContent=`${t.percent}%`),E&&(E.textContent=`${t.processed} / ${t.total} files`),M&&(M.textContent=t.stage),v(`${t.stage} · ${t.percent}%`)},V=document.querySelector("#archive-progress-dialog"),R=document.querySelector("#archive-progress-source"),k=document.querySelector("#archive-progress-file"),z=document.querySelector("#archive-progress-stage"),q=document.querySelector("#archive-progress-bar"),E=document.querySelector("#archive-progress-count"),A=document.querySelector("#archive-progress-percent"),M=document.querySelector("#archive-progress-status"),X=t=>{p&&(d="filesystem",T=t,K(!1),w&&(w.textContent=F),S&&(S.textContent=F),p.innerHTML=t.map((e,l)=>`
                    <div
                        class="file-row"
                        data-index="${l}"
                        tabindex="0"
                    >

                        <div class="cell name-cell">

                            <span class="file-icon">
                                ${N(e.icon)}
                            </span>

                            <span class="name-text">
                                ${e.name}
                            </span>

                        </div>

                        <div class="cell size-cell">
                            ${e.size===null?"":H(e.size)}
                        </div>

                        <div class="cell date-cell">
                            ${e.modified}
                        </div>

                        <div class="cell date-cell">
                            ${e.created}
                        </div>

                        <div class="cell"></div>
                        <div class="cell size-cell"></div>
                        <div class="cell size-cell"></div>
                        <div class="cell"></div>

                    </div>
                `).join(""),r=-1,p.querySelectorAll(".file-row").forEach(e=>{const l=Number(e.dataset.index);e.addEventListener("click",()=>{r=l,p.querySelectorAll(".file-row").forEach(o=>o.classList.remove("selected")),e.classList.add("selected"),u(`select	${t[l].path}`),C(),v(`1 / ${t.length} object(s) selected`)}),e.addEventListener("dblclick",()=>{const o=t[l];if(o){if(o.kind==="folder"){u(`directory	${o.path}`);return}if(o.name.toLowerCase().endsWith(".rip")){u(`archive	${o.path}`);return}u(`open-file	${o.path}`)}})}),v(`0 / ${t.length} object(s) selected`),C())},ie=t=>{const e=t.toLowerCase();return e.endsWith(".md")||e.endsWith(".markdown")?"markdown":e.endsWith(".mp4")||e.endsWith(".mkv")||e.endsWith(".avi")?"vlc":e.endsWith(".html")||e.endsWith(".htm")?"browser":e.endsWith(".exe")||e.endsWith(".dll")?"exe":e.endsWith(".iso")||e.endsWith(".img")?"disc":e.endsWith(".cpp")||e.endsWith(".hpp")||e.endsWith(".c")||e.endsWith(".h")||e.endsWith(".js")||e.endsWith(".ts")||e.endsWith(".tsx")||e.endsWith(".jsx")||e.endsWith(".py")||e.endsWith(".json")||e.endsWith(".css")?"vscode":"disc"},ae=(t,e,l,o,i)=>{p&&(d="archive",g=t,U=e,f="",q=i,I(l,o))},I=(t,e)=>{if(!p)return;d="archive",K(!0);const l=f.replace(/^\/+/,"").replace(/\/+$/,""),o=l.length>0?`${l}/`:"",i=new Set,c=[];for(const s of U){const n=s.name.replace(/^\/+/,"");if(!n.startsWith(o))continue;const a=n.slice(o.length);if(!a)continue;const y=a.indexOf("/");if(y!==-1){i.add(a.slice(0,y));continue}c.push(s)}const m=[...i].sort((s,n)=>s.localeCompare(n,void 0,{sensitivity:"base"})).map(s=>({kind:"folder",name:s,path:o+s}));c.sort((s,n)=>s.name.localeCompare(n.name,void 0,{sensitivity:"base"}));const b=[...m.map(s=>({type:"folder",name:s.name,path:s.path})),...c.map(s=>({type:"file",entry:s}))];p.innerHTML=b.map((s,n)=>{if(s.type==="folder")return`
                        <div
                            class="file-row"
                            data-index="${n}"
                            data-kind="folder"
                            tabindex="0"
                        >

                            <div class="cell name-cell">

                                <span class="file-icon">
                                    ${N("folder")}
                                </span>

                                <span class="name-text">
                                    ${s.name}
                                </span>

                            </div>

                            <div class="cell size-cell"></div>
                            <div class="cell size-cell"></div>
                            <div class="cell size-cell"></div>

                            <div class="cell">
                                <span class="method-label">
                                    Folder
                                </span>
                            </div>

                            <div class="cell"></div>
                            <div class="cell"></div>
                            <div class="cell"></div>

                        </div>
                    `;const a=s.entry,y=q?"—":a.originalSize>a.compressedSize?_(a.originalSize,a.compressedSize):"—";return`
                    <div
                        class="file-row"
                        data-index="${n}"
                        data-kind="file"
                        tabindex="0"
                    >

                        <div class="cell name-cell">

                            <span class="file-icon">
                                ${N(ie(a.name))}
                            </span>

                            <span class="name-text">
                                ${a.name.split("/").pop()??a.name}
                            </span>

                        </div>

                        <div class="cell size-cell">
                            ${H(a.originalSize)}
                        </div>

                        <div class="cell size-cell">
                            ${q?"shared":H(a.compressedSize)}
                        </div>

                        <div class="cell size-cell">
                            ${y}
                        </div>

                        <div class="cell">
                            <span
                                class="
                                    method-label
                                    ${a.method.toLowerCase()}
                                "
                            >
                                ${q?"RIPC SOLID":a.method}
                            </span>
                        </div>

                        <div class="cell"></div>
                        <div class="cell"></div>
                        <div class="cell"></div>

                    </div>
                `}).join(""),r=-1;const Y=()=>{v(`${b.length} item(s) · RIP ${t??1}.${e??0}`)};p.querySelectorAll(".file-row").forEach(s=>{const n=Number(s.dataset.index);s.addEventListener("click",()=>{r=n,p.querySelectorAll(".file-row").forEach(a=>a.classList.remove("selected")),s.classList.add("selected"),C(),v(`1 / ${b.length} object(s) selected`)}),s.addEventListener("dblclick",()=>{const a=b[n];if(a&&a.type==="folder"){f=a.path,I(t,e);return}})}),w&&(w.textContent=l.length>0?`${g}\\${l}`:g),S&&(S.textContent=l.length>0?`${g}\\${l}`:g),Y(),C()},le=t=>{let e;try{e=JSON.parse(t.data)}catch{return}if(e.type==="directory"){F=e.path,X(e.entries);return}if(e.type==="archive"){ae(e.path,e.entries,e.major,e.minor,e.solidRipc);return}if(e.type==="archiveProgress"){se(e);return}if(e.type==="archiveComplete"){te(),e.success?v("Archive created successfully"):v("Archive creation failed");return}if(e.type==="status"||e.type==="error"){v(e.text);return}};x?(x.addEventListener("message",le),u("ready")):(X([]),v("Run rip-gui.exe to browse the real filesystem"));document.querySelectorAll("[data-action]").forEach(t=>{t.addEventListener("click",()=>{const e=t.dataset.action;if(e){if(e==="add"){ee();return}u(`action	${e}`)}})});document.querySelector("#up-button")?.addEventListener("click",()=>{if(d==="archive"&&f.length>0){const t=f.split("/").filter(Boolean);t.pop(),f=t.join("/"),I();return}u("up")});
