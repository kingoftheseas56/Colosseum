<script>
  import { untrack } from 'svelte';

  let { initialModel, actions, preview = false } = $props();

  let model = $state(structuredClone(untrack(() => initialModel)));
  let selectedArrivalId = $state(model.arriving[0]?.id || '');
  let selectedLanded = $state(null);
  let filter = $state('All');
  let search = $state('');
  let notice = $state('');
  let confirmAction = $state(null);

  const filters = ['All', 'Theatre', 'Tankoban', 'Biblio', 'Audiobooks'];

  let selectedArrival = $derived(
    model.arriving.find(item => item.id === selectedArrivalId) || model.arriving[0] || null
  );

  let landedShown = $derived(model.landed.filter(item => {
    const inWorld = filter === 'All' || item.world === filter;
    const query = search.trim().toLowerCase();
    const haystack = (item.title + ' ' + item.subtitle + ' ' + item.world).toLowerCase();
    return inWorld && (!query || haystack.includes(query));
  }));

  function flash(message) {
    notice = message;
    setTimeout(() => {
      if (notice === message) notice = '';
    }, 2200);
  }

  function stateLabel(item) {
    if (item.state === 'failed') return 'Needs attention';
    if (item.state === 'paused') return 'Paused';
    if (item.state === 'downloading') return 'Arriving now';
    return item.state;
  }

  function togglePause(item) {
    if (!item || item.state === 'failed') return;
    const nextState = item.state === 'paused' ? 'downloading' : 'paused';
    model.arriving = model.arriving.map(row =>
      row.id === item.id ? { ...row, state: nextState } : row
    );
    if (nextState === 'paused') actions?.pause?.(item.id);
    else actions?.resume?.(item.id);
    flash(nextState === 'paused' ? 'Download paused' : 'Download resumed');
  }
  function retry(item) {
    model.arriving = model.arriving.map(row =>
      row.id === item.id
        ? { ...row, state: 'downloading', speed: '6.2 MB/s', eta: '3 min' }
        : row
    );
    flash('Trying another source');
  }

  function askCancel(item) {
    confirmAction = {
      title: 'Cancel download?',
      body: 'The partial files will be deleted.',
      label: 'Cancel download',
      run() {
        actions?.cancel?.(item.id);
        model.arriving = model.arriving.filter(row => row.id !== item.id);
        if (selectedArrivalId === item.id) {
          selectedArrivalId = model.arriving[0]?.id || '';
        }
        confirmAction = null;
        flash('Download cancelled');
      }
    };
  }

  function askDelete(item) {
    confirmAction = {
      title: 'Delete local copy?',
      body: 'The downloaded file will be deleted from this device.',
      label: 'Delete local copy',
      run() {
        actions?.delete?.(item.id);
        model.landed = model.landed.filter(row => row.id !== item.id);
        selectedLanded = null;
        confirmAction = null;
        flash('Local copy deleted');
      }
    };
  }

  function downloadHere(item) {
    if (!item.available) {
      flash('This item needs an available source first');
      return;
    }
    actions?.redownload?.(item.id);
    model.elsewhere = model.elsewhere.filter(row => row.id !== item.id);
    const world = item.subtitle.includes('Biblio')
      ? 'Biblio'
      : item.subtitle.includes('Tankoban') ? 'Tankoban' : 'Theatre';
    const id = 'remote-' + item.id;
    model.arriving = [{
      id,
      title: item.title,
      subtitle: item.subtitle.replace(/Laptop \d · /, '') + ' · queued here',
      world,
      state: 'downloading',
      progress: .04,
      received: '18 MB',
      total: '1.8 GB',
      speed: '5.1 MB/s',
      eta: '6 min',
      canPlay: false,
      tone: 'blue'
    }, ...model.arriving];
    selectedArrivalId = id;
    flash('Queued on this device');
  }

  function toggleBackground(item) {
    model.background = model.background.map(row =>
      row.id === item.id ? { ...row, paused: !row.paused } : row
    );
  }

  export function back() {
    if (confirmAction) {
      confirmAction = null;
      return true;
    }
    if (selectedLanded) {
      selectedLanded = null;
      return true;
    }
    return false;
  }
</script>

<svelte:head><title>Downloads · Colosseum</title></svelte:head>
<div class="stage">
  <div class="ambient ambient-a"></div>
  <div class="ambient ambient-b"></div>

  <main class="page" aria-label="Downloads">
    <header class="mast">
      <div>
        <div class="eyebrow">COLOSSEUM · LOCAL</div>
        <h1>Downloads</h1>
        <div class="gold-tick"></div>
      </div>

      <label class="search">
        <span>Search landed media</span>
        <input bind:value={search}
          placeholder="Title, world, format…"
          aria-label="Search downloaded media">
      </label>
    </header>

    <section class="command-strip" aria-label="Download summary">
      <div class="metric primary">
        <span>On this machine</span>
        <strong>{model.summary.bytes}</strong>
        <small>{model.summary.items} items</small>
      </div>
      <div class="metric">
        <span>Arriving</span>
        <strong>{model.arriving.filter(x => x.state !== 'failed').length}</strong>
        <small>active or paused</small>
      </div>
      <div class="metric">
        <span>Attention</span>
        <strong class:warn={model.arriving.some(x => x.state === 'failed')}>
          {model.arriving.filter(x => x.state === 'failed').length}
        </strong>
        <small>needs a decision</small>
      </div>
      <div class="storage">
        <div class="storage-copy">
          <span>Storage</span>
          <strong>{model.summary.free} free</strong>
          <small>of {model.summary.capacity}</small>
        </div>
        <div class="storage-track" aria-label="Storage used">
          <i style:width={(model.summary.usedRatio * 100) + '%'}></i>
        </div>
      </div>
      <div class="world-counts">
        {#each model.summary.worlds as world}
          <span><b>{world[1]}</b> {world[0]}</span>
        {/each}
      </div>
    </section>

    {#if model.arriving.length}
      <section class="section arrivals">
        <div class="section-head">
          <div>
            <span class="kicker">IN MOTION</span>
            <h2>Now arriving</h2>
          </div>
          <p>{model.arriving.length} transfers across your worlds</p>
        </div>

        <div class="arrival-grid">
          {#if selectedArrival}
            <article class="arrival-hero tone-{selectedArrival.tone}">
              <div class="hero-art">
                <div class="world-stamp">{selectedArrival.world}</div>
                <div class="hero-percent">
                  {Math.round(selectedArrival.progress * 100)}<small>%</small>
                </div>
                <div class="hero-state">{stateLabel(selectedArrival)}</div>
              </div>

              <div class="hero-copy">
                <div class="hero-topline">
                  <span>{selectedArrival.world}</span>
                  <span class:error={selectedArrival.state === 'failed'}>
                    {stateLabel(selectedArrival)}
                  </span>
                </div>
                <h3>{selectedArrival.title}</h3>
                <p>{selectedArrival.subtitle}</p>
                <div class="hero-progress">
                  <i style:width={(selectedArrival.progress * 100) + '%'}></i>
                </div>
                <div class="transfer-numbers">
                  <span><b>{selectedArrival.received}</b> of {selectedArrival.total}</span>
                  {#if selectedArrival.speed}<span><b>{selectedArrival.speed}</b></span>{/if}
                  {#if selectedArrival.eta}<span>{selectedArrival.eta} left</span>{/if}
                </div>

                <div class="hero-actions">
                  {#if selectedArrival.state === 'failed'}
                    <button class="primary-btn" data-focus
                      onclick={() => retry(selectedArrival)}>Retry</button>
                    <button data-focus
                      onclick={() => askCancel(selectedArrival)}>Dismiss</button>
                  {:else}
                    {#if selectedArrival.canPlay}
                      <button class="primary-btn" data-focus
                        onclick={() => flash('Opening arriving media')}>Play now</button>
                    {/if}
                    <button data-focus onclick={() => togglePause(selectedArrival)}>
                      {selectedArrival.state === 'paused' ? 'Resume' : 'Pause'}
                    </button>
                    <button class="quiet danger" data-focus
                      onclick={() => askCancel(selectedArrival)}>Cancel</button>
                  {/if}
                </div>
              </div>
            </article>
          {/if}

          <div class="queue" aria-label="Transfer queue">
            {#each model.arriving as item}
              <button class="queue-row"
                class:active={selectedArrivalId === item.id}
                class:error-row={item.state === 'failed'}
                data-focus onclick={() => selectedArrivalId = item.id}>
                <span class="queue-ring"
                  style={'--p:' + Math.round(item.progress * 100) + '%'}>
                  <i>{Math.round(item.progress * 100)}</i>
                </span>
                <span class="queue-copy">
                  <strong>{item.title}</strong>
                  <small>{item.world} · {stateLabel(item)}</small>
                </span>
                <span class="queue-number">
                  {item.speed || (item.state === 'failed' ? '!' : '—')}
                </span>
              </button>
            {/each}
          </div>
        </div>
      </section>
    {/if}

    <section class="section landed">
      <div class="section-head">
        <div>
          <span class="kicker">SETTLED</span>
          <h2>Landed here</h2>
        </div>
        <p>Open the thing, not a file path.</p>
      </div>

      <div class="filter-row" aria-label="Filter downloaded media">
        {#each filters as name}
          <button class:on={filter === name} data-focus
            onclick={() => filter = name}>{name}</button>
        {/each}
      </div>

      {#if landedShown.length}
        <div class="poster-grid">
          {#each landedShown as item}
            <button class="poster tone-{item.tone}" data-focus
              onclick={() => selectedLanded = item}>
              <span class="poster-world">{item.world}</span>
              <span class="poster-glyph">{item.title.slice(0, 1)}</span>
              <span class="poster-shade"></span>
              <span class="poster-copy">
                <strong>{item.title}</strong>
                <small>{item.subtitle}</small>
              </span>
              <span class="poster-action">{item.action}</span>
            </button>
          {/each}
        </div>
      {:else}
        <div class="empty">
          <strong>Nothing matches this shelf.</strong>
          <span>Try another world or clear the search.</span>
        </div>
      {/if}
    </section>

    {#if model.elsewhere.length}
      <section class="section elsewhere">
        <div class="section-head">
          <div>
            <span class="kicker">YOUR OTHER DEVICES</span>
            <h2>Available elsewhere</h2>
          </div>
          <p>The intent synced. The bytes did not.</p>
        </div>

        <div class="else-grid">
          {#each model.elsewhere as item}
            <article class="else-card">
              <div>
                <strong>{item.title}</strong>
                <span>{item.subtitle}</span>
              </div>
              <button class:muted={!item.available} data-focus
                onclick={() => downloadHere(item)}>
                {item.available ? 'Download here' : 'Source needed'}
              </button>
            </article>
          {/each}
        </div>
      </section>
    {/if}

    {#if model.background.length}
      <section class="section background">
        <div class="section-head compact">
          <div>
            <span class="kicker">AFTER THE DOWNLOAD</span>
            <h2>Preparing media</h2>
          </div>
          <p>Background work stays out of your way.</p>
        </div>

        <div class="activity-panel">
          {#each model.background as item}
            <div class="activity-row">
              <div class="activity-copy">
                <strong>{item.title}</strong>
                <span>{item.paused ? 'Paused' : item.stage}</span>
              </div>
              <div class="activity-progress">
                <i style:width={(item.progress * 100) + '%'}></i>
              </div>
              <span>{Math.round(item.progress * 100)}%</span>
              <button data-focus onclick={() => toggleBackground(item)}>
                {item.paused ? 'Resume' : 'Pause'}
              </button>
            </div>
          {/each}
        </div>
      </section>
    {/if}

    <footer>
      <span>{model.summary.items} local items</span>
      <span>{model.summary.bytes} on disk</span>
      {#if preview}<span>Interactive Svelte mock</span>{/if}
    </footer>
  </main>
  {#if selectedLanded}
    <div class="drawer-shade" role="presentation"
      onclick={() => selectedLanded = null}></div>
    <aside class="drawer" aria-label="Downloaded item details">
      <button class="drawer-close" data-focus aria-label="Close details"
        onclick={() => selectedLanded = null}>×</button>

      <div class="drawer-art tone-{selectedLanded.tone}">
        <span>{selectedLanded.world}</span>
        <b>{selectedLanded.title.slice(0, 1)}</b>
      </div>

      <div class="drawer-copy">
        <span class="kicker">{selectedLanded.world}</span>
        <h2>{selectedLanded.title}</h2>
        <p>{selectedLanded.subtitle}</p>
        <dl>
          <div><dt>On disk</dt><dd>{selectedLanded.size}</dd></div>
          <div><dt>Added</dt><dd>{selectedLanded.added}</dd></div>
          <div><dt>State</dt><dd>Ready offline</dd></div>
        </dl>
        <button class="primary-btn wide" data-focus
          onclick={() => {
            actions?.open?.(selectedLanded.id);
            flash('Opening ' + selectedLanded.title);
          }}>
          {selectedLanded.action}
        </button>
        <button class="delete-btn" data-focus
          onclick={() => askDelete(selectedLanded)}>Delete local copy</button>
      </div>
    </aside>
  {/if}

  {#if confirmAction}
    <div class="confirm-shade">
      <div class="confirm-card" role="dialog" aria-modal="true"
        aria-label={confirmAction.title}>
        <h3>{confirmAction.title}</h3>
        <p>{confirmAction.body}</p>
        <div>
          <button data-focus onclick={() => confirmAction = null}>Go back</button>
          <button class="primary-btn" data-focus
            onclick={confirmAction.run}>{confirmAction.label}</button>
        </div>
      </div>
    </div>
  {/if}

  {#if notice}
    <div class="toast">{notice}</div>
  {/if}
</div>

<style>
  @import './downloads.css';
</style>
