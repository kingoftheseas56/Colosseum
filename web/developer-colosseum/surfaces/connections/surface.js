// TrackerSyncCenterPage.qml:1560-2402 -> web Connections overview.
// The feed owns tracker facts. This surface only draws its safe presentation records.
(function (CW) {
  'use strict';
  const h = CW.h;
  const iconRoot = 'surfaces/connections/icons/';
  const providerIcons = {
    simkl: 'trackers/simkl.svg', mal: 'trackers/myanimelist.svg',
    trakt: 'trackers/trakt.svg', anilist: 'trackers/anilist.svg'
  };
  const subjects = {
    simkl: 'Movies, series, and anime', mal: 'Anime and manga',
    trakt: 'Movies and series', anilist: 'Anime and manga'
  };

  function icon(name, className) {
    return h('img.' + className, { src: iconRoot + name, alt: '', 'aria-hidden': true });
  }
  function providerIcon(key, className) {
    return providerIcons[key] ? icon(providerIcons[key], className) : null;
  }
  function displayStatus(status) {
    if (status === 'Healthy') return 'Confirmed';
    if (status === 'Owner unavailable') return 'Needs attention';
    return status || 'Unknown';
  }
  function deliveryState(state) {
    return {
      waiting: 'Waiting to sync', syncing: 'Syncing', retrying: 'Retry scheduled',
      checking_delivery: 'Checking delivery', needs_attention: 'Needs attention',
      failed: 'Could not deliver'
    }[state] || 'Delivery status unknown';
  }
  function deliveryReason(reason) {
    return {
      none: 'Saved in Colosseum. Waiting for the tracker to sync.',
      acknowledgement_lost: 'The tracker may have received this. Colosseum checks before any retry.',
      delivery_uncertain: 'The tracker may have received this. Colosseum checks before any retry.',
      authentication_required: 'Reconnect this tracker before Colosseum resumes delivery.',
      unsupported_action: 'This tracker does not support this action. Colosseum keeps its own progress.',
      provider_unavailable: 'Colosseum saved the change locally and is waiting for the tracker.',
      provider_retry: 'Colosseum saved the change locally and is waiting for the tracker.',
      rate_limited: 'The tracker asked Colosseum to wait before continuing.',
      local_state_changed: 'Colosseum progress changed while this update was waiting. Review is needed.',
      title_match_changed: 'The title match changed while this update was waiting. Review is needed.',
      destination_review_required: 'This update came from another profile. Review it here before sending.'
    }[reason] || 'Colosseum has kept this update for recovery.';
  }
  function lastSync(ms) {
    const value = Number(ms) || 0;
    if (value <= 0) return 'Never';
    const minutes = Math.max(0, Math.floor((Date.now() - value) / 60000));
    if (minutes < 1) return 'Just now';
    if (minutes < 60) return minutes + ' min ago';
    const hours = Math.floor(minutes / 60);
    return hours < 24 ? hours + ' h ago' : Math.floor(hours / 24) + ' d ago';
  }
  function tag(label) { return h('span.connections-tag', {}, label); }
  function unavailable(section) {
    return CW.section.note('Connections unavailable', section.error || 'This section could not be loaded.', 'err');
  }

  CW.router.register('page.connections', {
    mount(el, route, env) {
      const page = h('div.connections-page');
      const body = h('div.connections-sections');
      const overlay = h('div.connections-overlay-host');
      page.append(body, overlay);
      el.appendChild(page);
      let sub = null;
      let revision = null;
      let currentRoute = route;
      let request = 0;
      let openerKey = '';
      const overlayFocus = new MutationObserver(() => {
        if (!overlay.firstChild || overlay.contains(document.activeElement)) return;
        requestAnimationFrame(() => {
          const first = overlay.querySelector('[data-focus]:not(:disabled)');
          if (first && !overlay.contains(document.activeElement)) first.focus({ preventScroll: true });
        });
      });
      overlayFocus.observe(overlay, { childList: true });

      function refreshFeed() {
        if (sub) sub.close();
        sub = env.port.subscribe('page.connections', {}, receive);
      }

      function openDossier(key) {
        if (revision === null) return;
        openerKey = document.activeElement && document.activeElement.getAttribute('data-key') || '';
        env.router.go({ name: 'page', page: 'connections', params: { dossier: key } });
      }

      function openReview(review, extra) {
        const key = currentRoute.params && currentRoute.params.dossier;
        if (!key) return;
        env.router.go({ name: 'page', page: 'connections',
          params: { dossier: key, review, ...(extra || {}) } });
      }

      function reviewShell(title, description, content, footer) {
        return h('div.connections-veil', {},
          h('div.connections-veil-shade', { onclick: () => env.router.back() }),
          h('div.connections-review', { role: 'dialog', 'aria-label': title },
            h('div.connections-review-head', {}, h('h2', {}, title),
              h('button.connections-dossier-close', { type: 'button',
                'data-focus': true, 'data-key': 'connections.review.close',
                'aria-label': 'Close review', onclick: () => env.router.back() }, '×')),
            h('p.connections-review-description', {}, description),
            h('div.connections-review-scroll', {}, content),
            footer || null));
      }

      const choiceLabels = {
        use_provider_progress: 'Use tracker progress',
        keep_colosseum: 'Keep Colosseum progress',
        leave_unmatched: 'Leave unmatched',
        leave_unresolved: 'Leave for later',
        find_match: 'Find match'
      };
      const classifications = {
        new_progress: 'New progress', exact_match: 'Already matches',
        remote_advance: 'Tracker is ahead', disagreement: 'Progress differs',
        needs_matching: 'Needs a title match', unsupported: 'Not supported',
        duplicate: 'Duplicate entry'
      };
      function importProgress(row) {
        const tracker = row.providerCompleted ? 'Completed' : 'Episode ' + row.providerProgress;
        if (!row.hasLocalAtPreview) return 'Tracker: ' + tracker + ' · No Colosseum progress yet';
        const local = row.localCompleted ? 'Completed' : 'Episode ' + row.localProgress;
        return 'Tracker: ' + tracker + ' · Colosseum: ' + local;
      }
      function canConfirmImport(snapshot) {
        return !!snapshot.pageComplete && !snapshot.confirmed
          && (snapshot.items || []).every(row => row.state !== 'review_required' && row.state !== 'applying');
      }

      function renderImportReview(snapshot, notice) {
        // TrackerSyncCenterPage.qml:801-1142,3303-3714. Native gives each
        // item's allowed choices; incomplete pages never enable confirm.
        const batchId = snapshot.batchId;
        const reviewRevision = snapshot.revision;
        const selected = new Set();
        const rows = snapshot.items || [];
        const groupButton = h('button.connections-panel-button', { type: 'button',
          'data-focus': true, 'data-key': 'connections.import.group', disabled: true,
          onclick: () => openReview('bulk', { batchId, itemIds: [...selected] })
        }, 'Review group choices');
        const common = () => {
          const chosen = rows.filter(row => selected.has(row.reviewItemId));
          if (chosen.length < 2 || chosen.some(row => row.state !== 'review_required')) return [];
          return (chosen[0].allowedChoices || []).filter(choice => choice !== 'find_match'
            && chosen.every(row => (row.allowedChoices || []).includes(choice)));
        };
        const cards = rows.map(row => h('div.connections-import-row', {},
          h('div.connections-import-row-head', {},
            row.state === 'review_required' ? h('input', { type: 'checkbox',
              'data-focus': true, 'data-key': 'connections.import.select.' + row.reviewItemId,
              'aria-label': 'Select ' + (row.title || 'untitled entry') + ' for group review',
              onchange: event => {
                if (event.target.checked) selected.add(row.reviewItemId);
                else selected.delete(row.reviewItemId);
                groupButton.disabled = common().length === 0;
              } }) : null,
            h('strong', {}, row.title || 'Untitled entry'),
            h('span', {}, classifications[row.classification] || 'Needs review')),
          h('p', {}, importProgress(row)),
          row.nativeHistoryProtected ? h('p.connections-warning', {},
            'Native History protects this title. Keep Colosseum progress is the only available choice.') : null,
          h('div.connections-choice-row', {}, (row.allowedChoices || []).map(choice =>
            h('button.connections-panel-button', { type: 'button', 'data-focus': true,
              'data-key': 'connections.import.' + row.reviewItemId + '.' + choice,
              onclick: () => {
                if (choice === 'find_match') {
                  openReview('match', { batchId, itemId: row.reviewItemId,
                    remoteTitle: row.title || 'Untitled tracker item' });
                  return;
                }
                env.act('page.connections.resolveImport', {
                  batchId, itemId: row.reviewItemId, choice, revision: reviewRevision
                }).then(answer => {
                  if (answer && answer.ok) {
                    revision = answer.result.revision;
                    refreshFeed();
                    loadImportReview(batchId, 'Choice saved in this preview. Nothing was applied before import confirmation.');
                  }
                });
              }
            }, choiceLabels[choice] || 'Unavailable'))),
          !(row.allowedChoices || []).length ? h('small', {}, row.state === 'unresolved'
            ? 'Left unmatched or for later · Colosseum progress was not changed'
            : row.state === 'waiting_to_apply' ? 'Approved · waiting for Colosseum to apply progress'
            : row.state === 'applied' ? 'Applied to progress · History remains native'
            : 'No change needed') : null));
        const confirm = h('button.connections-panel-button.connections-review-primary', {
          type: 'button', 'data-focus': true, 'data-key': 'connections.import.confirm',
          disabled: !canConfirmImport(snapshot),
          onclick: () => {
            confirm.disabled = true;
            confirm.textContent = 'Applying import…';
            env.act('page.connections.confirmImport', {
            batchId, revision: reviewRevision
          }).then(answer => {
            if (answer && answer.ok) {
              revision = answer.result.revision;
              refreshFeed();
              loadImportReview(batchId, 'Review confirmed. Colosseum History and Activity remain native-owned.');
            } else {
              confirm.disabled = false;
              confirm.textContent = 'Confirm import review';
            }
          });
          }
        }, snapshot.confirmed ? 'Review confirmed' : 'Confirm import review');
        overlay.replaceChildren(reviewShell('Review import from ' + (snapshot.providerName || 'tracker'),
          snapshot.pageComplete
            ? 'Review differences individually before Colosseum applies tracker progress.'
            : 'This provider has more pages to read. Colosseum has not confirmed the import.',
          h('div.connections-import-list', {},
            notice ? h('p.connections-dossier-notice', { role: 'status' }, notice) : null,
            cards.length ? cards : h('p', {}, 'There are no items in this review.'), groupButton),
          h('div.connections-review-footer', {}, confirm,
            h('button.connections-panel-button', { type: 'button', 'data-focus': true,
              'data-key': 'connections.import.cancel', onclick: () => env.router.back() }, 'Back'))));
      }

      function loadImportReview(batchId, notice) {
        if (revision === null) return;
        const serial = ++request;
        overlay.replaceChildren(reviewShell('Review import', 'Loading the current preview…'));
        env.act('page.connections.importReview', { batchId, revision }).then(answer => {
          if (serial !== request || !(currentRoute.params && currentRoute.params.review === 'import')) return;
          if (!answer || !answer.ok) {
            overlay.replaceChildren(reviewShell('Review unavailable',
              answer && answer.error || 'This review could not be opened.',
              h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                'data-key': 'connections.review.close', onclick: () => env.router.back() }, 'Back')));
            return;
          }
          revision = answer.result.revision;
          renderImportReview(answer.result, notice);
          requestAnimationFrame(() => {
            const target = overlay.querySelector('[data-key="connections.review.close"]');
            if (target) target.focus({ preventScroll: true });
          });
        });
      }

      function renderExportReview(snapshot, notice) {
        // TrackerSyncCenterPage.qml:1398-1468,3071-3302.
        const selected = new Set();
        const items = snapshot.items || [];
        const reviewRevision = snapshot.revision;
        const count = h('span', {}, '0 selected');
        const confirm = h('button.connections-panel-button.connections-review-primary', {
          type: 'button', 'data-focus': true, 'data-key': 'connections.export.confirm', disabled: true,
          onclick: () => env.act('page.connections.confirmExport', {
            reviewId: snapshot.reviewId, itemIds: [...selected], revision: reviewRevision
          }).then(answer => {
            if (answer && answer.ok) {
              revision = answer.result.revision;
              refreshFeed();
              env.toast(answer.result.notice);
              env.router.back();
            }
          })
        }, 'Send 0 to ' + (snapshot.providerName || 'tracker'));
        const rows = items.map(row => h('label.connections-export-row', {},
          h('input', { type: 'checkbox', disabled: !row.eligible,
            'data-focus': true, 'data-key': 'connections.export.select.' + row.itemId,
            onchange: event => {
              if (event.target.checked) selected.add(row.itemId);
              else selected.delete(row.itemId);
              count.textContent = selected.size + ' selected';
              confirm.textContent = 'Send ' + selected.size + ' to ' + (snapshot.providerName || 'tracker');
              confirm.disabled = selected.size === 0;
            } }),
          h('span', {}, h('strong', {}, row.title || 'Colosseum item'),
            h('span', {}, (row.kind || 'Update') + (row.kind === 'Progress' ? ' ' + row.progress : '')),
            h('small', {}, row.willChangeRemote
              ? 'Tracker now: ' + (row.remoteBefore || 'Existing tracker state')
                + ' → after send: ' + (row.remoteAfter || 'Local update') + ' · ' + (row.reason || '')
              : row.reason || ''))));
        overlay.replaceChildren(reviewShell('Send to ' + (snapshot.providerName || 'tracker'),
          'Choose the Colosseum updates you want to send. This review is separate from importing tracker data.',
          h('div.connections-export-list', {},
            notice ? h('p.connections-dossier-notice', {}, notice) : null,
            snapshot.eligibleCount === 0 ? h('p', {}, items.length
              ? 'No local updates can be sent from this review. The reasons are shown below; close this review.'
              : 'There are no local updates to send. Close this review.') : null,
            rows),
          h('div.connections-review-footer', {}, count, confirm)));
      }

      function loadExportReview(key) {
        if (revision === null) return;
        const serial = ++request;
        overlay.replaceChildren(reviewShell('Send review', 'Preparing current Colosseum updates…'));
        env.act('page.connections.beginExport', { providerKey: key, revision }).then(answer => {
          if (serial !== request || !(currentRoute.params && currentRoute.params.review === 'export')) return;
          if (!answer || !answer.ok) {
            overlay.replaceChildren(reviewShell('Send review unavailable',
              answer && answer.error || 'The review could not be prepared.',
              h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                'data-key': 'connections.review.close', onclick: () => env.router.back() }, 'Back')));
            return;
          }
          revision = answer.result.revision;
          renderExportReview(answer.result);
        });
      }

      function loadBulkReview(batchId, itemIds) {
        // TrackerSyncCenterPage.qml:827-979. A group choice is offered only
        // when the current native preview allows it for every selected item.
        const serial = ++request;
        overlay.replaceChildren(reviewShell('Review group choices', 'Loading current import choices…'));
        env.act('page.connections.importReview', { batchId, revision }).then(answer => {
          if (serial !== request || currentRoute.params?.review !== 'bulk') return;
          if (!answer?.ok) {
            overlay.replaceChildren(reviewShell('Group review changed',
              answer?.error || 'Open this import review again.'));
            return;
          }
          const snapshot = answer.result;
          const selected = (snapshot.items || []).filter(row => itemIds.includes(row.reviewItemId)
            && row.state === 'review_required');
          const choices = selected.length === itemIds.length && selected.length >= 2
            ? (selected[0].allowedChoices || []).filter(choice => choice !== 'find_match'
              && selected.every(row => (row.allowedChoices || []).includes(choice))) : [];
          overlay.replaceChildren(reviewShell('Review ' + selected.length + ' import choices',
            'One choice will be applied to these selected preview items. Nothing is imported yet.',
            h('div.connections-import-list', {}, selected.map(row =>
              h('p.connections-import-row', {}, row.title || 'Untitled tracker item')),
              choices.length ? h('div.connections-choice-row', {}, choices.map(choice =>
                h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                  'data-key': 'connections.bulk.' + choice,
                  onclick: () => env.act('page.connections.resolveImportMany', {
                    batchId, itemIds, choice, revision: snapshot.revision
                  }).then(result => {
                    if (!result?.ok) return;
                    revision = result.result.revision;
                    refreshFeed();
                    env.router.back();
                  })
                }, choiceLabels[choice] || choice)))
                : h('p', {}, 'These entries no longer share a review choice. Go back and select them again.'))));
        });
      }

      function loadTitleMatch(batchId, itemId, initialQuery) {
        // TrackerSyncCenterPage.qml:402-548,3715-3951. Native issues each
        // candidate handle; selecting one never marks a title watched.
        const serial = ++request;
        const matchRevision = revision;
        const query = h('input.connections-match-query', { type: 'search', value: initialQuery || '',
          'data-focus': true, 'data-key': 'connections.match.query',
          'aria-label': 'Find a Colosseum title' });
        const results = h('div.connections-import-list');
        const search = h('button.connections-panel-button', { type: 'button',
          'data-focus': true, 'data-key': 'connections.match.search',
          onclick: runSearch }, 'Find match');
        query.addEventListener('keydown', event => {
          if (event.key === 'Enter') { event.preventDefault(); runSearch(); }
        });
        function runSearch() {
          results.replaceChildren(h('p', {}, 'Searching Colosseum titles…'));
          env.act('page.connections.titleMatches', {
            batchId, itemId, query: query.value, revision: matchRevision
          }).then(answer => {
            if (serial !== request || currentRoute.params?.review !== 'match') return;
            if (!answer?.ok) {
              results.replaceChildren(CW.section.note('Match unavailable',
                answer?.error || 'Open this import review again.', 'err'));
              return;
            }
            const rows = answer.result.items || [];
            results.replaceChildren(...(rows.length ? rows.map(row =>
              h('button.connections-dossier-row', { type: 'button', 'data-focus': true,
                'data-key': 'connections.match.' + row.candidateId,
                onclick: () => env.act('page.connections.confirmTitleMatch', {
                  batchId, itemId, candidateId: row.candidateId, revision: matchRevision
                }).then(result => {
                  if (!result?.ok) return;
                  revision = result.result.revision;
                  refreshFeed();
                  env.router.back();
                })
              }, h('strong', {}, row.displayName || 'Untitled title'),
              h('span', {}, [row.mediaType, row.displayContext].filter(Boolean).join(' · '))))
              : [h('p', {}, 'No matching Colosseum title found. Leave this tracker item unmatched.') ]));
          });
        }
        overlay.replaceChildren(reviewShell('Find a title match',
          'Choose an exact title, then return to decide whether to import its progress.',
          h('div.connections-match', {}, query, search, results)));
        runSearch();
      }

      function setting(label, value, enabled, key, action, providerKey) {
        const button = h('button.connections-setting', {
          type: 'button', 'data-focus': true,
          'data-key': 'connections.setting.' + key,
          role: 'switch', 'aria-checked': value ? 'true' : 'false',
          disabled: !enabled,
          onclick: () => {
            button.disabled = true;
            const payload = providerKey
              ? { providerKey, setting: key, enabled: !value, revision }
              : { key, enabled: !value, revision };
            env.act(action, payload).then(answer => {
              if (answer && answer.ok) {
                revision = answer.result.revision;
                refreshFeed();
              }
              loadDossier(providerKey || 'global', answer && answer.ok
                ? answer.result.notice : (answer && answer.error) || 'That preference could not be saved.');
            });
          }
        }, h('span', {}, label), h('span.connections-switch' + (value ? '.on' : ''),
          { 'aria-hidden': true }, h('span')));
        return button;
      }

      function focusDeliveryIssue(key) {
        // TrackerSyncCenterPage.qml:1219-1303. Only native chooses an issue
        // and its current row index; the web page merely focuses that row.
        env.act('page.connections.diagnose', { providerKey: key, revision }).then(answer => {
          if (!answer?.ok || currentRoute.params?.dossier !== key) return;
          const index = answer.result.focusIndex;
          const row = overlay.querySelector('[data-key="connections.delivery.' + index + '"]');
          if (row) row.focus({ preventScroll: false });
        });
      }

      function dossierView(result, notice) {
        const d = result.dossier || {};
        const key = d.providerKey || 'global';
        const global = key === 'global';
        const importReviews = result.importReviews || [];
        const delivery = result.deliveryRows || [];
        const capabilities = d.capabilities || [];
        const capabilityLabels = {
          read_history: 'History import', read_progress: 'Progress import',
          write_progress: 'Progress export', write_completion: 'Completion export',
          scrobble: 'Playback tracking'
        };
        const close = h('button.connections-dossier-close', {
          type: 'button', 'data-focus': true, 'data-key': 'connections.dossier.close',
          'aria-label': 'Close tracker details', onclick: () => env.router.back()
        }, '×');
        const header = h('div.connections-dossier-head', {},
          global ? icon('preferences.svg', 'connections-dossier-icon')
            : providerIcon(key, 'connections-dossier-icon'),
          h('div', {}, h('span.connections-kicker', {}, global ? 'ALL TRACKERS'
            : d.connected ? 'CONNECTED TRACKER' : d.pendingWork ? 'PAUSED TRACKER WORK' : 'AVAILABLE TRACKER'),
            h('h2', {}, d.providerName || 'Tracker')),
          close);
        const health = h('div.connections-dossier-health', {},
          h('div', {}, h('strong', {}, displayStatus(d.status)),
            h('span.connections-kicker', {}, d.connected ? 'THIS PROFILE' : 'CAPABILITY GATE')),
          h('p', {}, global ? 'Trackers are optional. Colosseum saves local changes first.'
            : !d.available ? "This provider is unavailable in this build. Colosseum's native progress, History, and activity remain available."
            : d.pendingWork ? 'Tracker work remains paused on this profile. Reconnect the same account to check uncertain delivery before any retry.'
            : d.connected ? 'Provider status and actions follow the verified capability snapshot.'
            : 'Connect only when this build has a verified provider authentication path.'),
          !global && !d.connected ? h('button.connections-panel-button', {
            type: 'button', disabled: !d.connectEnabled || !d.available
          }, d.pendingWork ? 'Reconnect to recover pending work' : d.available ? 'Connect' : 'Not available') : null);
        const blocks = [health];
        if (global) {
          // TrackerSyncCenterPage.qml:3000-3049.
          blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Current preferences'),
            setting('Tracker sync', !!d.trackerSyncEnabled, !!d.editable,
              'trackerSyncEnabled', 'page.connections.globalSetting'),
            setting('Check connected trackers on launch', !!d.checkOnLaunch, !!d.editable,
              'checkOnLaunch', 'page.connections.globalSetting'),
            setting('Deliver in the background', !!d.backgroundDelivery, !!d.editable,
              'backgroundDelivery', 'page.connections.globalSetting'),
            setting('Show completion messages', !!d.completionMessages, !!d.editable,
              'completionMessages', 'page.connections.globalSetting'),
            !d.editable ? h('p', {}, 'Settings are unavailable for this profile.') : null));
        } else {
          if (d.connected) blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Account and profile'),
            h('p', {}, d.accountLabel || 'Connected account'),
            h('small', {}, 'Only this Colosseum profile can use this connection')));
          blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Capabilities'),
            capabilities.length ? h('div.connections-capabilities', {},
              capabilities.map(value => capabilityLabels[value] ? tag(capabilityLabels[value]) : null))
              : h('p', {}, 'No capabilities are verified in this build.')));
          if (importReviews.length) blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Import review'),
            importReviews.map(row => h('button.connections-dossier-row', { type: 'button',
              'data-focus': true, 'data-key': 'connections.batch.' + row.batchId,
              onclick: () => openReview('import', { batchId: row.batchId }) },
              h('strong', {}, row.initialImport ? 'Initial import' : 'Progress update'),
              h('span', {}, String(row.reviewCount || 0) + ' items for review')))));
          if (delivery.length) blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Delivery status'),
            !d.connected ? h('p', {}, Number(d.unknownOutcomeCount) > 0
              ? 'Uncertain delivery stays paused until this same account reconnects. Colosseum checks before retrying.'
              : 'Pending tracker work stays paused until this account reconnects.') : null,
            (d.status === 'Attention' || Number(d.unknownOutcomeCount) > 0)
              ? h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                'data-key': 'connections.delivery.diagnose',
                onclick: () => focusDeliveryIssue(key) }, 'Check current issue') : null,
            delivery.map((row, index) => h('div.connections-dossier-row', {
              tabindex: '0', 'data-focus': true, 'data-key': 'connections.delivery.' + index,
              'aria-label': (row.title || 'Playback update') + ', ' + deliveryState(row.state)
            }, h('strong', {}, row.title || 'Playback update'),
            h('span', {}, deliveryState(row.state)),
            h('small', {}, deliveryReason(row.reason))))));
          if (d.connected) blocks.push(h('div.connections-dossier-block', {},
            h('h3', {}, 'Automatic sync'),
            setting('Pull changes automatically', !!d.pullAutomatically, !!d.pullSettingEnabled,
              'pull', 'page.connections.providerSetting', key),
            setting('Send Colosseum progress', !!d.sendProgressEnabled, !!d.sendSettingEnabled,
              'send', 'page.connections.providerSetting', key),
            !d.exportReviewed && (capabilities.includes('write_progress')
              || capabilities.includes('write_completion'))
              ? h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                'data-key': 'connections.export.open', disabled: !d.exportReviewEnabled,
                onclick: () => openReview('export') },
                'Review Colosseum updates before sending') : null,
            setting('Live playback tracking', !!d.livePlaybackTrackingEnabled,
              !!d.livePlaybackSettingEnabled, 'live', 'page.connections.providerSetting', key)));
          // TrackerSyncCenterPage.qml:2657-2710. Destructive actions open a
          // current, revision-pinned native preview before any mutation.
          if (d.disconnectEnabled || d.cleanupEnabled || d.removeImportedEnabled)
            blocks.push(h('div.connections-dossier-block', {},
              h('h3', {}, 'Connection and imported data'),
              d.disconnectEnabled || d.cleanupEnabled
                ? h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                  'data-key': 'connections.disconnect.open',
                  onclick: () => openReview('disconnect') },
                  d.cleanupEnabled ? 'Review paused work' : 'Disconnect tracker') : null,
              d.removeImportedEnabled
                ? h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                  'data-key': 'connections.remove.open',
                  onclick: () => openReview('remove') }, 'Remove imported data') : null));
        }
        return h('div.connections-veil', {},
          h('div.connections-veil-shade', { onclick: () => env.router.back() }),
          h('div.connections-dossier', { role: 'dialog', 'aria-label': d.providerName || 'Tracker details' },
            header, h('div.connections-dossier-scroll', {},
              notice ? h('p.connections-dossier-notice', { role: 'status' }, notice) : null,
              blocks)));
      }

      function loadDossier(key, notice) {
        if (revision === null || currentRoute.params && currentRoute.params.dossier !== key) return;
        const serial = ++request;
        overlay.replaceChildren(h('div.connections-veil', {},
          h('div.connections-veil-shade'),
          h('div.connections-dossier.connections-dossier-loading', {}, 'Loading connection details…')));
        env.act('page.connections.dossier', { providerKey: key, revision }).then(answer => {
          if (serial !== request || !(currentRoute.params && currentRoute.params.dossier === key)) return;
          if (!answer || !answer.ok) {
            overlay.replaceChildren(h('div.connections-veil', {},
              h('div.connections-veil-shade', { onclick: () => env.router.back() }),
              h('div.connections-dossier', { role: 'dialog', 'aria-label': 'Connection details' },
                CW.section.note('Could not open details', answer && answer.error || 'Please try again.', 'err'),
                h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                  'data-key': 'connections.dossier.close', onclick: () => env.router.back() }, 'Close'))));
            return;
          }
          revision = answer.result.revision;
          overlay.replaceChildren(dossierView(answer.result, notice));
          requestAnimationFrame(() => {
            const target = overlay.querySelector('[data-key="connections.dossier.close"]');
            if (target) target.focus({ preventScroll: true });
          });
        });
      }

      function loadChoiceReview(key, kind) {
        // TrackerSyncCenterPage.qml:549-800,3952-4470. Fresh dossier rows
        // provide the count, protected-data wording and action eligibility.
        const serial = ++request;
        overlay.replaceChildren(reviewShell('Review connection', 'Loading the current tracker state…'));
        env.act('page.connections.dossier', { providerKey: key, revision }).then(answer => {
          if (serial !== request || currentRoute.params?.review !== kind) return;
          if (!answer?.ok) {
            overlay.replaceChildren(reviewShell('Review unavailable',
              answer?.error || 'Open this tracker again to review its current state.'));
            return;
          }
          const d = answer.result.dossier;
          const pinnedRevision = answer.result.revision;
          if (kind === 'disconnect') {
            const cleanup = !d.connected && d.cleanupEnabled;
            if (!d.disconnectEnabled && !cleanup) {
              overlay.replaceChildren(reviewShell('Disconnect unavailable',
                'The tracker or its pending work changed. Open its current status again.'));
              return;
            }
            const count = Number(d.knownUnsentCount) || 0;
            const uncertain = Number(d.unknownOutcomeCount) || 0;
            const choices = cleanup ? [
              ['discard_known_unsent', 'Retry removing known-unsent updates']
            ] : [
              ['keep_paused', 'Disconnect and keep pending work'],
              ['discard_known_unsent', 'Disconnect and discard known-unsent work']
            ];
            const buttons = choices.map(([choice, label]) =>
              h('button.connections-panel-button', { type: 'button', 'data-focus': true,
                'data-key': 'connections.disconnect.' + choice,
                onclick: () => {
                  buttons.forEach(button => { button.disabled = true; });
                  env.act('page.connections.disconnect', {
                    providerKey: key, choice, revision: pinnedRevision
                  }).then(result => {
                    if (!result?.ok) {
                      buttons.forEach(button => { button.disabled = false; });
                      return;
                    }
                    revision = result.result.revision;
                    refreshFeed();
                    env.toast(choice === 'discard_known_unsent'
                      ? 'Known-unsent updates were discarded. Uncertain outcomes remain paused.'
                      : 'Disconnected. Pending tracker work remains paused for recovery.');
                    env.router.back();
                  });
                }
              }, label));
            overlay.replaceChildren(reviewShell(cleanup ? 'Review paused work' : 'Disconnect ' + d.providerName,
              'Pending: ' + (Number(d.pendingCount) || 0) + ' · Known unsent: ' + count
                + ' · Uncertain outcome: ' + uncertain,
              h('div.connections-import-list', {},
                h('p', {}, 'This removes the Colosseum connection. Revoke access separately in the tracker account’s connected-app settings.'),
                uncertain ? h('p.connections-warning', {}, uncertain
                  + ' update(s) may already have reached the tracker. They stay paused until reconnect checks them.') : null,
                buttons)));
            return;
          }
          const rows = d.importedDataRemovalPreview || [];
          if (!d.removeImportedEnabled || !rows.length) {
            overlay.replaceChildren(reviewShell('Imported data changed',
              'Open this tracker again to review its current imported entries.'));
            return;
          }
          const confirm = h('button.connections-panel-button.connections-review-primary', {
            type: 'button', 'data-focus': true, 'data-key': 'connections.remove.confirm',
            onclick: () => {
              confirm.disabled = true;
              confirm.textContent = 'Removing imported data…';
              env.act('page.connections.removeImported', {
                providerKey: key, revision: pinnedRevision
              }).then(result => {
                if (!result?.ok) {
                  confirm.disabled = false;
                  confirm.textContent = 'Remove imported data';
                  return;
                }
                revision = result.result.revision;
                refreshFeed();
                env.toast(result.result.notice);
                env.router.back();
              });
            }
          }, 'Remove imported data');
          overlay.replaceChildren(reviewShell('Remove ' + d.providerName + ' imported data',
            'Affected: ' + (Number(d.importedHistoryCount) || 0) + ' History evidence and '
              + (Number(d.importedProgressCount) || 0) + ' eligible Progress entries.',
            h('div.connections-import-list', {},
              h('p.connections-warning', {}, 'Native History, Activity, statistics, local Progress and other trackers’ data stay. No remote tracker data changes.'),
              rows.map(row => h('div.connections-import-row', {},
                h('strong', {}, row.title || 'Untitled media'),
                h('span', {}, row.dataKind || 'Tracker data'),
                h('p', {}, row.consequence || 'Only this tracker’s imported data is affected.')))),
            h('div.connections-review-footer', {}, confirm)));
        });
      }

      function syncRoute(next) {
        currentRoute = next;
        const key = next.params && next.params.dossier;
        ++request;
        if (!key) {
          body.inert = false;
          overlay.replaceChildren();
          if (openerKey) requestAnimationFrame(() => {
            const target = body.querySelector('[data-key="' + CSS.escape(openerKey) + '"]');
            if (target) target.focus({ preventScroll: true });
          });
          return;
        }
        body.inert = true;
        const review = next.params && next.params.review;
        if (review === 'import' && next.params.batchId)
          loadImportReview(String(next.params.batchId));
        else if (review === 'bulk' && next.params.batchId)
          loadBulkReview(String(next.params.batchId), Array.isArray(next.params.itemIds)
            ? next.params.itemIds.map(String) : []);
        else if (review === 'match' && next.params.batchId && next.params.itemId)
          loadTitleMatch(String(next.params.batchId), String(next.params.itemId),
            String(next.params.remoteTitle || ''));
        else if (review === 'export')
          loadExportReview(String(key));
        else if (review === 'disconnect' || review === 'remove')
          loadChoiceReview(String(key), review);
        else
          loadDossier(String(key));
      }

      function receive(event) {
        CW.section.sync(body, event, ctx);
        const header = (event.sections || []).find(section => section.id === 'connections.header');
        const nextRevision = header?.data?.revision;
        const changed = nextRevision != null && revision != null
          && String(nextRevision) !== String(revision);
        if (nextRevision != null) revision = nextRevision;
        const key = currentRoute.params && currentRoute.params.dossier;
        if (key && changed && currentRoute.params.review) {
          // TrackerSyncCenterPage.qml:1470-1519. Never let a refreshed feed
          // silently replace the pinned revision of an open review.
          ++request;
          overlay.replaceChildren(reviewShell('Review changed',
            'Connection information changed while this review was open. Go back and open the current review.'));
        } else if (key && changed) {
          loadDossier(String(key));
        } else if (key && !overlay.firstChild) syncRoute(currentRoute);
      }

      const ctx = {
        custom(section) {
          if (section.state === 'error') return unavailable(section);
          const d = section.data || {};
          switch (d.schema) {
            case 'connections.header':
              // TrackerSyncCenterPage.qml:1560-1670. Native validates the
              // revision and connected-provider eligibility for Sync all.
              return h('div.connections-head', {},
                h('div.connections-heading', {},
                  h('h1', {}, 'Connections'),
                  h('p', {}, 'Your external library connections')),
                h('div.connections-head-actions', {},
                  h('div.connections-action-row', {},
                    h('button.connections-secondary', { type: 'button', 'data-focus': true,
                      'data-key': 'connections.preferences',
                      'aria-label': 'Connection preferences', onclick: () => openDossier('global') },
                      icon('preferences.svg', 'connections-action-icon'), 'Preferences'),
                    h('button.connections-secondary.connections-sync-all', {
                      type: 'button', 'data-focus': true, 'data-key': 'connections.sync-all',
                      disabled: !(d.aggregate && d.aggregate.canSyncAll),
                      'aria-label': 'Sync all connected trackers',
                      onclick: () => env.act('page.connections.syncAll', { revision }).then(answer => {
                        if (answer && answer.ok) {
                          revision = answer.result.revision;
                          env.toast(answer.result.notice);
                          refreshFeed();
                        }
                      })
                    }, icon('sync.svg', 'connections-action-icon'), 'Sync all')),
                  Number(d.aggregate && d.aggregate.connectedCount) === 0
                    ? h('span.connections-aggregate', {}, d.summary || '') : null));
            case 'connections.native':
              // TrackerSyncCenterPage.qml:1671-1741.
              return h('div.connections-native-card', {},
                h('div.connections-native-symbol', {}, icon('colosseum.svg', 'connections-colosseum-icon')),
                h('span.connections-native-divider', { 'aria-hidden': true }),
                h('div.connections-native-copy', {},
                  h('span.connections-kicker', {}, 'NATIVE LIBRARY'),
                  h('h2', {}, d.name || 'Colosseum'),
                  h('strong', {}, d.statement || ''),
                  h('p', {}, d.detail || '')),
                h('span.connections-available', {}, '✓  ' + (d.badge || 'Always available')));
            case 'connections.relay':
              // TrackerSyncCenterPage.qml:1742-1936.
              return h('div.connections-relay', {},
                h('div.connections-relay-top', {},
                  h('span.connections-kicker', {}, 'CONNECTION STATUS'),
                  h('span', {}, d.summary || '')),
                h('div.connections-relay-map', {},
                  h('div.connections-canonical', {},
                    icon('colosseum.svg', 'connections-canonical-icon'),
                    h('div', {}, h('strong', {}, 'Colosseum'), h('span', {}, 'CANONICAL'))),
                  h('span.connections-relay-stem', { 'aria-hidden': true }),
                  h('div.connections-relay-providers', {}, (d.providers || []).map(row =>
                    h('div.connections-relay-provider', {},
                      providerIcon(row.providerKey, 'connections-provider-icon'),
                      h('div', {}, h('strong', {}, row.providerName || 'Tracker'),
                        h('span', {}, displayStatus(row.status))))))));
            case 'connections.attention': {
              // TrackerSyncCenterPage.qml:1936-1990.
              const unresolved = Number(d.unresolvedCount) || 0;
              const count = Number(d.attentionProviderCount) || 0;
              return h('div.connections-attention', {},
                h('strong.connections-attention-mark', {}, '!'),
                h('strong', {}, unresolved > 0
                  ? unresolved + ' tracker items need review' : count + ' trackers need attention'),
                h('span', {}, 'Native History is unchanged.'));
            }
            case 'connections.connected':
              // TrackerSyncCenterPage.qml:1991-2140.
              return h('div.connections-connected', {},
                h('div.connections-section-heading', {}, h('h2', {}, 'Your trackers'),
                  h('span', {}, 'This profile')),
                h('div.connections-connected-grid', {}, (d.providers || []).map(row =>
                  h('button.connections-connected-card', { type: 'button',
                    'data-focus': true, 'data-key': 'connections.provider.' + row.providerKey,
                    'aria-label': (row.providerName || 'Tracker') + ', ' + displayStatus(row.status),
                    onclick: () => openDossier(row.providerKey) },
                    h('div.connections-connected-top', {},
                      providerIcon(row.providerKey, 'connections-provider-icon'),
                      h('div.connections-connected-name', {},
                        h('strong', {}, row.providerName || 'Tracker'),
                        h('span', {}, row.accountLabel || 'Connected to this profile')),
                      h('span', {}, displayStatus(row.status))),
                    h('div.connections-connected-facts', {},
                      h('div', {}, h('strong', {}, lastSync(row.lastSuccessfulSyncAtMs)),
                        h('span', {}, 'LAST SYNC')),
                      h('div', {}, h('strong', {}, String((Number(row.unresolvedCount) || 0)
                        + (Number(row.waitingCount) || 0))), h('span', {}, 'WAITING / REVIEW')),
                      h('span.connections-open', {}, 'Open ›'))))));
            case 'connections.stremio':
              // TrackerSyncCenterPage.qml:2141-2279. Status is explicitly
              // unavailable until the shared Stremio projection is supplied.
              return h('div.connections-stremio-section', {},
                h('div.connections-section-heading.connections-section-stacked', {},
                  h('h2', {}, 'External services'),
                  h('p', {}, 'Manage connections to external libraries.')),
                h('div.connections-stremio-card', {},
                  icon('stremio-official.svg', 'connections-stremio-icon'),
                  h('div.connections-stremio-copy', {},
                    h('h3', {}, d.name || 'Stremio'),
                    h('p', {}, 'Library, progress, and History'),
                    h('div.connections-tags', {}, (d.capabilities || []).map(tag))),
                  h('div.connections-stremio-actions', {},
                    h('span.connections-state-badge', {}, d.status || 'Status unavailable'),
                    h('button.connections-open-settings', {
                      type: 'button', disabled: !d.panelAvailable,
                      'aria-label': 'Open Stremio settings'
                    }, 'Open settings ›'))));
            case 'connections.catalogue':
              // TrackerSyncCenterPage.qml:2280-2402. Availability and verified
              // capability claims are taken directly from native cards.
              return h('div.connections-catalogue', {},
                h('div.connections-section-heading', {}, h('h2', {}, 'Available trackers'),
                  h('span', {}, 'Capabilities are verified per provider')),
                h('div.connections-catalogue-grid', {}, (d.providers || []).map(row =>
                  h('button.connections-catalogue-card', { type: 'button',
                    'data-focus': true, 'data-key': 'connections.catalogue.' + row.providerKey,
                    'aria-label': (row.providerName || 'Tracker') + ', '
                      + (row.status || 'Unavailable'),
                    onclick: () => openDossier(row.providerKey) },
                    h('div.connections-catalogue-main', {},
                      providerIcon(row.providerKey, 'connections-catalogue-icon'),
                      h('div', {}, h('strong', {}, row.providerName || 'Tracker'),
                        h('span', {}, subjects[row.providerKey] || ''))),
                    h('span.connections-state-badge', {}, row.available
                      ? (row.connected ? 'Connected'
                        : row.pendingWork ? 'Needs attention' : 'Connect')
                      : 'Unavailable in this build')))));
            default:
              return CW.section.note('Unknown Connections record', 'This section is not supported.', 'err');
          }
        }
      };
      refreshFeed();
      syncRoute(route);
      return {
        update(next) { syncRoute(next); },
        unmount() { ++request; overlayFocus.disconnect(); if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
