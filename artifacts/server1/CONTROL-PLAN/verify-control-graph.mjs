import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REQUIRED_AUTHORITY_INPUTS = [
  'EXECUTION-WAVES.md',
  'PARALLEL-DEPENDENCIES.mmd',
  'PARALLEL-EXECUTION-PLAN.md',
  'PARALLEL-WORK-ITEMS.json',
  'SELF-REVIEW.md',
  'SHARED-OWNERSHIP.md',
  'VERIFICATION-RESULTS.json',
  '_generate_parallel_plan.mjs',
  '_verify_parallel_plan.mjs'
];
const REQUIRED_WORKERS = ['P08-T', 'H02-C', 'INT-W4', 'C02-D', 'Q02-P'];
const WORKER_SEMANTIC_FIELDS = [
  'kind',
  'parent_packet',
  'wave',
  'objective',
  'dependencies',
  'interfaces_consumed',
  'interfaces_produced',
  'owned_files',
  'acceptance_tests',
  'integration_owner',
  'review_gate',
  'cases',
  'status'
];
const EXPECTED_WORKER_SEMANTIC_SHA256 = {
  'P08-T': '63754114cc366a27d41e73ae7c3d52c1b1b33edc4fa7c3e4eba2b31217ce4c7c',
  'H02-C': '1f8a4c577fa6b4b1c2ec3cb8958cff35caaafbd02009b8340829cc70f2611006',
  'INT-W4': '9fe46b5341a6de494bc9ebc3bd70dae3f5cbbd08e76a8b28bda8852b81edb1e2',
  'C02-D': '83d3ed6a9e11863000294f19402cce065c5c1e657871034486d1e81b78a41f9c',
  'Q02-P': '4d0b6d49b1c775d7d609eb0560dfa60e6948115bab72f6a2c2848428bf74701f'
};
const REQUIRED_EDGES = [
  'K02-A->K06-A',
  'K09-A->K09-B',
  'M05-A->M05-B',
  'Q00-A->Q00-B',
  'Q01-A->Q01-B',
  'P08-A->P08-T',
  'B-W3C->P08-T',
  'P08-T->K10-A',
  'B-W3G->H02-C',
  'P08-T->H02-C',
  'H02-C->H02-B',
  'B-W3G->INT-W4',
  'INT-W4->B-W4A',
  'INT-W4->B-W4B',
  'B-W9A->C02-D',
  'C02-D->C02-A',
  'C02-D->C02-B',
  'B-W10A->Q02-P',
  'C02-D->Q02-P',
  'Q02-P->Q02-A',
  'Q02-P->Q02-B',
  'G-ROLLBACK-ARTIFACT->Q04-A'
];
const REQUIRED_ALIASES = {
  'HlsV2RouteSurface->G-MEDIA-V2': {
    producer: 'M06-B',
    source: 'HlsV2RouteSurface',
    target: 'G-MEDIA-V2',
    consumers: ['C00-A', 'C00-B', 'C00-C']
  },
  'G-PLATFORM->DesktopPlatformQualification': {
    producer: 'C02-C',
    source: 'G-PLATFORM',
    target: 'DesktopPlatformQualification',
    consumers: ['Q00-A', 'Q00-B', 'Q01-A', 'Q01-B', 'Q02-P', 'Q04-A']
  }
};
const REQUIRED_OWNERS = {
  'torrent-transport-public-contract': 'P08-T',
  'standalone-torrent-host': 'H02-C',
  'w4-state-checkpoint-mutation': 'INT-W4',
  'desktop-release-package-mutation': 'C02-D',
  'assembled-app-torrent-playback-probe': 'Q02-P'
};
const EXPECTED_OWNER_SHA256 = {
  'torrent-transport-public-contract': '263ab72fd15f0ba505556da50a90c32d94a92d52a69061bcad51124a2e82f27c',
  'standalone-torrent-host': '8a7cfa749e0004aca5db8abc11b7cda06f86a846d605fc5d2c0c0db88c0d0734',
  'w4-state-checkpoint-mutation': '0c7eeac869843cce4b95a13258ad3c2666b16417d242a2663d85c26a8582970f',
  'desktop-release-package-mutation': 'f4b7d7f5097903c38c713a1dfdbf7cdd5c3d4acda689813b769d898155cffc6e',
  'assembled-app-torrent-playback-probe': '79845317ab8b52049d4ec7cec15ae16a5e379b5c7821dcefb7cb9e270fb7434d'
};
const EXPECTED_WORKER_PATCHES_ALGORITHM = 'sha256(JSON.stringify(worker_patches) encoded as UTF-8)';
const EXPECTED_WORKER_PATCHES_SHA256 = 'ee01121885874f6980fbc76f68c7f2ba3561307753471fd5c8b9e74c98029b64';
const EXPECTED_EFFECTIVE_WORKER_SHA256 = {
  'P03-A': 'f013dbebdfa53015b17da80d20cc9c251c76f0d1d92049ce612d20383ed2a600',
  'K10-A': '27011426d192e3d872d9b2faa40ee404a9bebbdbeb60e8d99b01602f35d7d7bf',
  'H02-B': '47b26a7f5f31794824b66dd21dcfbe76943e16af2b58c3b8c4b935cfe85988c4',
  'E00-B': '9bac25eae5c7980db5ec27c0cb2a454b7383a4a07d0164ccad18c4ef9081bf13',
  'C02-A': 'd6a590987b137c199044ba85ab27a11055252ae4c3593eb0e153e8623e8f592d',
  'C02-B': 'f7e7b12082217c86743907318c6485eef2131fbe6169986bc959d5d21676dc5f',
  'Q02-A': 'c67aec8f0673ed986240f5d093941a31f9fe0179bf260d4288be4571388ca470',
  'Q02-B': 'e4c5d6f4e6d0947a352960cefd15c16672aac0d9461ac73aa789bbe4fc076901',
  'Q04-A': '808cff47fb3c453d8ffb79eda99dc1ad5538203f440e174fba5a9afb8c0831d7',
  'P08-T': '82efdcc83eafb64edae458ed20ddb2f941e380ad63bed6c5ab92290b9fd9cef2',
  'H02-C': 'f1e487cc8ce85d31c6d3f0a135e95c38cf73545bd2c928debf3e43ba8abf1f8c',
  'INT-W4': '8cc629c41e1b0d27a910135bdb035adba1c490c5c913894c8b3fb843f389a71b',
  'C02-D': '9e6d129c7f65caaff3d4948658f51d2c478bae1d3d167d4d15039c08f834fe90',
  'Q02-P': '42cfa8bf1c3ef5ae781fdc557db920585e506e0fadd664b13ff6c9fdf38b2391'
};
const W4_CHECKPOINT_PATHS = [
  'docs/server1/checkpoints/G-TORRENT.json',
  'docs/server1/checkpoints/G-EMBED-EARLY.json'
];
const W4_AUTHORITY_PATHS = ['docs/server1/STATE.json', ...W4_CHECKPOINT_PATHS];
const EXPECTED_PROTECTED_PATH_OWNERS = {
  'native/colosseum_server_v1/include/server1/ports/TorrentTransport.h': ['P08-T'],
  'native/colosseum_server_v1/host/main.cpp': ['H02-C'],
  'native/colosseum_server_v1/tests/test_standalone_torrent_host.cpp': ['H02-C'],
  'native/colosseum_server_v1/CMakeLists.txt': ['P03-A', 'INT-W1', 'INT-W2', 'INT-W3', 'INT-W4', 'INT-W5', 'INT-W6', 'INT-W7'],
  'native/colosseum_server_v1/tests/CMakeLists.txt': ['P03-A', 'INT-W1', 'INT-W2', 'INT-W3', 'INT-W4', 'INT-W5', 'INT-W6', 'INT-W7'],
  'docs/server1/STATE.json': ['P00-A', 'INT-W1', 'INT-W2', 'INT-W3', 'INT-W4', 'INT-W5', 'INT-W6', 'INT-W7'],
  'docs/server1/checkpoints/G-TORRENT.json': ['INT-W4'],
  'docs/server1/checkpoints/G-EMBED-EARLY.json': ['INT-W4'],
  'native/build-target.bat': ['C02-D'],
  'scripts/installer/package_release.sh': ['C02-D'],
  'scripts/installer/colosseum.nsi': ['C02-D'],
  'scripts/publish_app_release.py': ['C02-D'],
  '.github/workflows/release-installer-smoke.yml': ['C02-D'],
  'docs/build/windows.md': ['C02-D'],
  'docs/build/linux.md': ['C02-D'],
  'tests/test_server1_release_package.py': ['C02-D'],
  'tools/server_lab/scenarios/assembled_app_torrent_playback.json': ['Q02-P'],
  'tools/server_lab/tests/test_assembled_app_observations.py': ['Q02-P'],
  'artifacts/server1/Q02/Q02-P/': ['Q02-P']
};
const EXPECTED_DISPATCH_SHA256 = '29086ce7b252eba17ba39251285329f958ec3909fcc6cc13848c22932104433b';
const EXPECTED_DISPATCH_STAGE_IDS = [
  'W3-foundation',
  'W3-piece-store',
  'W3-policy-fanout',
  'W3-swarm-caps',
  'W3-transport-contract',
  'W3-native-adapter',
  'W4-host-and-adapter',
  'W4-torrent-qualification',
  'W4-torrent-barrier',
  'W4-embed-audit',
  'W4-embed-integration',
  'W4-embed-barrier',
  'W7-initial',
  'W7-ffmpeg-arguments',
  'W9-app-integration',
  'W9-package-and-other-platform',
  'W9-desktop-qualification',
  'W9-platform-convergence',
  'W10-parity-and-stress',
  'W10-accounting',
  'W10-lifecycle',
  'W10-assembled-app-probe',
  'W10-playback-and-soak',
  'W10-playback-convergence'
];
const EXPECTED_BARRIER_OWNER_SHA256 = {
  'B-W4A': 'b9a62d2ea1d981bc00113e01dfd5592f45239d854fdddca57bca5bce6a087245',
  'B-W4B': '269b6b2d64d550a1e2db8473637c2e08f56b1664148c9bd394687c4690a2cb27'
};
const EXPECTED_ROLLBACK_SHA256 = '68f9faf248aac80560db449ba76e7dc6fc0820a972103dfb6af5eeebb2390835';
const EXPECTED_BASE_INVARIANTS_SHA256 = '20b309b117f397773c9fe8f80485a40f6ed1352f95415abf8d2ef2a79f3a4241';
const EXPECTED_MANIFEST_ALGORITHM = 'sha256(filename-sorted manifest lines encoded as UTF-8: <file_sha256><two spaces><basename><LF>)';
const EXPECTED_COMPOSITION_FILE_OWNERS = {
  'native/colosseum_server_v1/src/Runtime.cpp': 'C00-C',
  'native/colosseum_server_v1/src/ServerComposition.cpp': 'C00-C',
  'native/colosseum_server_v1/src/http/RootRoutes.cpp': 'C00-B'
};
const REQUIRED_CONTRACTS = {
  TorrentTransportContract: {
    producer: 'P08-T',
    consumers: ['K10-A', 'K11-A', 'H02-A', 'H02-B', 'H02-C', 'C03-A', 'C03-B']
  },
  StandaloneTorrentHost: {
    producer: 'H02-C',
    consumers: ['H02-B']
  },
  DesktopReleasePackage: {
    producer: 'C02-D',
    consumers: ['C02-A', 'C02-B', 'Q02-P']
  },
  AssembledAppTorrentPlaybackObservation: {
    producer: 'Q02-P',
    consumers: ['Q02-A', 'Q02-B']
  }
};

const sorted = values => [...values].sort();
const sameSet = (actual, expected) =>
  JSON.stringify(sorted(new Set(actual))) === JSON.stringify(sorted(new Set(expected)));
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const array = value => Array.isArray(value) ? value : [];
const objectProjection = (value, fields) => Object.fromEntries(fields.map(field => [field, value?.[field]]));
const objectSha256 = value => {
  const serialized = JSON.stringify(value);
  return sha256(Buffer.from(serialized === undefined ? 'undefined' : serialized, 'utf8'));
};

export class ControlGraphValidationError extends Error {
  constructor(failures) {
    super(`control graph validation failed (${failures.length})`);
    this.name = 'ControlGraphValidationError';
    this.failures = failures;
  }
}

function applyWorkerPatches(workerMap, patches, check) {
  for (const patch of array(patches)) {
    const worker = workerMap.get(patch.worker_id);
    check(Boolean(worker), `worker patch target exists: ${patch.worker_id}`);
    if (!worker) continue;
    for (const [addField, field] of [
      ['add_interfaces_consumed', 'interfaces_consumed'],
      ['add_interfaces_produced', 'interfaces_produced'],
      ['add_owned_files', 'owned_files']
    ]) {
      if (patch[addField]) worker[field] = [...new Set([...array(worker[field]), ...array(patch[addField])])];
    }
    for (const [removeField, field] of [
      ['remove_interfaces_consumed', 'interfaces_consumed'],
      ['remove_interfaces_produced', 'interfaces_produced'],
      ['remove_owned_files', 'owned_files']
    ]) {
      if (patch[removeField]) {
        const remove = new Set(array(patch[removeField]));
        worker[field] = array(worker[field]).filter(value => !remove.has(value));
      }
    }
    if ('replace_integration_owner' in patch) worker.integration_owner = patch.replace_integration_owner;
  }
}

function interfaceIndex(workerMap, field) {
  const index = new Map();
  for (const worker of workerMap.values()) {
    for (const name of array(worker[field])) {
      if (!index.has(name)) index.set(name, []);
      index.get(name).push(worker.worker_id);
    }
  }
  return index;
}

export function validateControlGraph({ basePlan, addendum, authorityRoot }) {
  const failures = [];
  const pass = [];
  const check = (condition, name, detail = '') => {
    (condition ? pass : failures).push(detail ? `${name}: ${detail}` : name);
  };

  check(addendum?.schema === 'colosseum-server1-control-graph-addendum/v1', 'recognized addendum schema');
  check(addendum?.plan_version === '2.1', 'plan version remains 2.1');
  check(addendum?.base_commit === 'cbcae97986bd7891326f7835623847e14c28a8f8', 'accepted B-W2B base is pinned');

  const authorityInputs = array(addendum?.frozen_authority?.inputs);
  check(sameSet(authorityInputs.map(input => input.path), REQUIRED_AUTHORITY_INPUTS), 'all frozen authority inputs are pinned exactly once');
  check(authorityInputs.length === new Set(authorityInputs.map(input => input.path)).size, 'authority input paths are unique');
  const manifestLines = [];
  let verifiedAuthorityInputs = 0;
  for (const input of sorted(authorityInputs.map(entry => entry.path))) {
    const entry = authorityInputs.find(candidate => candidate.path === input);
    const file = authorityRoot ? path.join(authorityRoot, input) : '';
    check(Boolean(authorityRoot), 'frozen authority root supplied');
    check(Boolean(file) && fs.existsSync(file), `frozen authority input exists: ${input}`);
    if (!file || !fs.existsSync(file)) continue;
    const bytes = fs.readFileSync(file);
    const digest = sha256(bytes);
    check(bytes.length === entry.bytes, `frozen authority byte count matches: ${input}`, `${bytes.length}/${entry.bytes}`);
    check(digest === entry.sha256, `frozen authority SHA256 matches: ${input}`, `${digest}/${entry.sha256}`);
    if (bytes.length === entry.bytes && digest === entry.sha256) verifiedAuthorityInputs++;
    manifestLines.push(`${digest}  ${input}\n`);
  }
  const packageDigest = sha256(Buffer.from(manifestLines.join(''), 'utf8'));
  check(addendum?.frozen_authority?.manifest_algorithm === EXPECTED_MANIFEST_ALGORITHM, 'package digest algorithm is exact and filename-sorted');
  check(packageDigest === addendum?.frozen_authority?.package_sha256, 'frozen authority package digest matches', packageDigest);

  const frozenResultPath = authorityRoot ? path.join(authorityRoot, 'VERIFICATION-RESULTS.json') : '';
  let frozenResult = null;
  if (frozenResultPath && fs.existsSync(frozenResultPath)) {
    try {
      frozenResult = JSON.parse(fs.readFileSync(frozenResultPath, 'utf8'));
    } catch (error) {
      failures.push(`frozen verification result parses: ${error.message}`);
    }
  }
  check(frozenResult?.verdict === 'VERIFIED', 'frozen graph verifier verdict is VERIFIED');
  check(frozenResult?.checks_passed === 57 && frozenResult?.checks_failed === 0, 'frozen graph verifier remains 57/57');

  const packetCount = array(basePlan?.master_packets).length;
  const caseIds = array(basePlan?.workers).flatMap(worker => array(worker.cases));
  check(packetCount === 64, 'all 64 master packets preserved', String(packetCount));
  check(caseIds.length === 193 && new Set(caseIds).size === 193, 'all 193 cases preserved exactly once', String(caseIds.length));
  check(addendum?.base_invariants?.master_packets === 64, 'addendum pins 64 master packets');
  check(addendum?.base_invariants?.planned_cases === 193, 'addendum pins 193 cases');
  check(addendum?.base_invariants?.master_packet_outcomes_changed === false, 'master packet outcomes are unchanged');
  check(addendum?.base_invariants?.case_assignments_changed === false, 'case assignments are unchanged');
  check(addendum?.base_invariants?.frozen_public_contracts_changed === false, 'frozen public contracts are unchanged in this slice');
  check(objectSha256(addendum?.base_invariants) === EXPECTED_BASE_INVARIANTS_SHA256, 'base composition and route invariants are exact');

  const addedWorkers = array(addendum?.workers);
  check(sameSet(addedWorkers.map(worker => worker.worker_id), REQUIRED_WORKERS), 'all five graph-repair workers are present exactly');
  check(addedWorkers.length === new Set(addedWorkers.map(worker => worker.worker_id)).size, 'added worker IDs are unique');
  const baseWorkerIds = new Set(array(basePlan?.workers).map(worker => worker.worker_id));
  for (const worker of addedWorkers) {
    check(!baseWorkerIds.has(worker.worker_id), `added worker does not collide with frozen graph: ${worker.worker_id}`);
    check(worker.status === 'planned', `added worker remains planned: ${worker.worker_id}`);
    check(array(worker.cases).length === 0, `added worker does not change case inventory: ${worker.worker_id}`);
    check(array(worker.owned_files).length > 0, `added worker has bounded ownership: ${worker.worker_id}`);
    check(array(worker.acceptance_tests).length > 0, `added worker has mandatory acceptance tests: ${worker.worker_id}`);
    check(
      objectSha256(objectProjection(worker, WORKER_SEMANTIC_FIELDS)) === EXPECTED_WORKER_SEMANTIC_SHA256[worker.worker_id],
      `worker semantic fields are exact: ${worker.worker_id}`
    );
  }

  const workerMap = new Map();
  for (const worker of [...array(basePlan?.workers), ...addedWorkers]) {
    workerMap.set(worker.worker_id, structuredClone(worker));
  }
  const workerPatches = array(addendum?.worker_patches);
  const workerPatchesDigest = objectSha256(workerPatches);
  const workerPatchesExact =
    addendum?.worker_patches_integrity?.algorithm === EXPECTED_WORKER_PATCHES_ALGORITHM &&
    addendum?.worker_patches_integrity?.sha256 === EXPECTED_WORKER_PATCHES_SHA256 &&
    workerPatchesDigest === EXPECTED_WORKER_PATCHES_SHA256;
  check(addendum?.worker_patches_integrity?.algorithm === EXPECTED_WORKER_PATCHES_ALGORITHM, 'worker patch digest algorithm is exact');
  check(addendum?.worker_patches_integrity?.sha256 === EXPECTED_WORKER_PATCHES_SHA256, 'worker patch digest declaration is exact');
  check(workerPatchesDigest === EXPECTED_WORKER_PATCHES_SHA256, 'worker patch set and ordering are exact');
  if (workerPatchesExact) applyWorkerPatches(workerMap, workerPatches, check);
  for (const [workerId, expectedDigest] of Object.entries(EXPECTED_EFFECTIVE_WORKER_SHA256)) {
    const worker = workerMap.get(workerId);
    check(Boolean(worker), `effective worker exists: ${workerId}`);
    check(objectSha256(worker) === expectedDigest, `effective worker record is exact: ${workerId}`);
  }

  const edgeRecords = array(addendum?.required_edges);
  check(sameSet(edgeRecords.map(edge => edge.id), REQUIRED_EDGES), 'every required dependency and dispatch edge is declared');
  check(edgeRecords.length === new Set(edgeRecords.map(edge => edge.id)).size, 'required edge IDs are unique');
  for (const edge of edgeRecords) {
    check(edge.id === `${edge.from}->${edge.to}`, `edge ID matches endpoints: ${edge.id}`);
    check(typeof edge.reason === 'string' && edge.reason.length > 0, `edge has rationale: ${edge.id}`);
  }

  const conditionalDependencies = array(addendum?.conditional_dependencies);
  const rollback = conditionalDependencies.find(rule => rule.id === 'Q04-ROLLBACK-ARTIFACT');
  check(conditionalDependencies.length === 1 && Boolean(rollback), 'exactly one Q04 rollback dependency rule exists');
  check(rollback?.consumer_worker === 'Q04-A', 'rollback rule gates Q04-A');
  check(rollback?.virtual_gate === 'G-ROLLBACK-ARTIFACT', 'rollback rule exposes the required virtual gate');
  check(rollback?.produces_interface === 'RollbackArtifact', 'rollback gate produces RollbackArtifact');
  check(rollback?.selection === 'exactly-one-approved-option', 'rollback artifact selection is exclusive and approved');
  check(sameSet(array(rollback?.options).map(option => option.id), ['buildable-p01b', 'approved-current-sidecar']), 'rollback rule has only the two approved alternatives');
  const sidecarOption = array(rollback?.options).find(option => option.id === 'approved-current-sidecar');
  check(array(sidecarOption?.required_evidence).some(item => item.includes('explicit Agent 4 approval')), 'current-sidecar rollback requires specific approval');
  const p01bOption = array(rollback?.options).find(option => option.id === 'buildable-p01b');
  check(p01bOption?.source_worker === 'P01B-A', 'buildable rollback alternative is P01B-A');
  check(objectSha256(rollback) === EXPECTED_ROLLBACK_SHA256, 'rollback alternatives and every evidence requirement are exact');

  const virtualNodes = new Set(conditionalDependencies.map(rule => rule.virtual_gate));
  const dependencies = new Map();
  for (const worker of workerMap.values()) dependencies.set(worker.worker_id, [...array(worker.dependencies)]);
  for (const barrier of array(basePlan?.barriers)) dependencies.set(barrier.id, [...array(barrier.dependencies)]);
  for (const virtualNode of virtualNodes) dependencies.set(virtualNode, []);
  for (const edge of edgeRecords) {
    check(dependencies.has(edge.from), `edge source exists: ${edge.id}`);
    check(dependencies.has(edge.to), `edge target exists: ${edge.id}`);
    if (dependencies.has(edge.to) && !dependencies.get(edge.to).includes(edge.from)) dependencies.get(edge.to).push(edge.from);
  }
  for (const workerId of REQUIRED_WORKERS) {
    const worker = workerMap.get(workerId);
    const declaredIncoming = edgeRecords.filter(edge => edge.to === workerId).map(edge => edge.from);
    check(Boolean(worker) && sameSet(array(worker.dependencies), declaredIncoming), `new worker dependencies are fully declared as required edges: ${workerId}`);
  }
  for (const [node, deps] of dependencies) {
    for (const dependency of deps) check(dependencies.has(dependency), `graph dependency exists: ${dependency}->${node}`);
  }

  const indegree = new Map([...dependencies].map(([node, deps]) => [node, deps.length]));
  const outgoing = new Map([...dependencies.keys()].map(node => [node, []]));
  for (const [node, deps] of dependencies) {
    for (const dependency of deps) if (outgoing.has(dependency)) outgoing.get(dependency).push(node);
  }
  const queue = [...indegree].filter(([, degree]) => degree === 0).map(([node]) => node);
  let visited = 0;
  while (queue.length) {
    const node = queue.shift();
    visited++;
    for (const next of outgoing.get(node) || []) {
      indegree.set(next, indegree.get(next) - 1);
      if (indegree.get(next) === 0) queue.push(next);
    }
  }
  check(visited === dependencies.size, 'amended worker and barrier graph is acyclic', `${visited}/${dependencies.size}`);
  const reaches = (from, to) => {
    const seen = new Set([from]);
    const pending = [from];
    while (pending.length) {
      const node = pending.shift();
      if (node === to) return true;
      for (const next of outgoing.get(node) || []) {
        if (!seen.has(next)) {
          seen.add(next);
          pending.push(next);
        }
      }
    }
    return false;
  };
  for (const edge of edgeRecords) check(reaches(edge.from, edge.to), `required edge is reachable: ${edge.id}`);
  const dispatchWaves = array(addendum?.dispatch_overrides).map(override => override.wave);
  check(sameSet(dispatchWaves, ['W3', 'W4', 'W7', 'W9', 'W10']), 'dispatch overrides cover every corrected wave');
  const dispatchOverrides = array(addendum?.dispatch_overrides);
  const dispatchStages = dispatchOverrides.flatMap(override => array(override.stages));
  check(sameSet(dispatchStages.map(stage => stage.id), EXPECTED_DISPATCH_STAGE_IDS), 'every required dispatch stage is present exactly');
  check(dispatchStages.length === new Set(dispatchStages.map(stage => stage.id)).size, 'dispatch stage IDs are unique');
  check(objectSha256(dispatchOverrides) === EXPECTED_DISPATCH_SHA256, 'dispatch stages and ordering are exact');
  for (const stage of dispatchStages) {
    check(array(stage.after).length > 0, `dispatch stage has prerequisites: ${stage.id}`);
    check(array(stage.release).length > 0, `dispatch stage has releases: ${stage.id}`);
    for (const prerequisite of array(stage.after)) {
      check(dependencies.has(prerequisite), `dispatch prerequisite exists: ${stage.id}:${prerequisite}`);
      for (const released of array(stage.release)) {
        check(dependencies.has(released), `dispatch release exists: ${stage.id}:${released}`);
        check(reaches(prerequisite, released), `dispatch prerequisite precedes release: ${stage.id}:${prerequisite}->${released}`);
      }
    }
  }

  const barrierOwnerOverlays = array(addendum?.barrier_owner_overrides);
  check(sameSet(barrierOwnerOverlays.map(overlay => overlay.barrier_id), Object.keys(EXPECTED_BARRIER_OWNER_SHA256)), 'both W4 barrier-owner overlays are present');
  check(barrierOwnerOverlays.length === new Set(barrierOwnerOverlays.map(overlay => overlay.barrier_id)).size, 'W4 barrier-owner overlays are unique');
  const barrierOwners = {};
  for (const [barrierId, expectedDigest] of Object.entries(EXPECTED_BARRIER_OWNER_SHA256)) {
    const overlay = barrierOwnerOverlays.find(candidate => candidate.barrier_id === barrierId);
    check(Boolean(overlay), `barrier-owner overlay exists: ${barrierId}`);
    if (!overlay) continue;
    check(objectSha256(overlay) === expectedDigest, `barrier-owner overlay fields are exact: ${barrierId}`);
    check(overlay.owner_worker === 'INT-W4', `INT-W4 owns barrier acceptance/state/checkpoint: ${barrierId}`);
    check(overlay.ownership === 'barrier-acceptance-state-and-checkpoint', `barrier ownership kind is exact: ${barrierId}`);
    check(workerMap.get(overlay.producer_worker)?.integration_owner === 'INT-W4', `frozen producer ownership is reconciled: ${barrierId}`);
    check(array(workerMap.get('INT-W4')?.owned_files).includes(overlay.checkpoint_path), `INT-W4 owns checkpoint path: ${barrierId}`);
    check(array(workerMap.get('INT-W4')?.owned_files).includes(overlay.state_path), `INT-W4 owns state path: ${barrierId}`);
    check(array(dependencies.get(barrierId)).includes(overlay.owner_worker), `barrier depends on INT-W4: ${barrierId}`);
    check(array(dependencies.get(barrierId)).includes(overlay.producer_worker), `barrier depends on its producer: ${barrierId}`);
    barrierOwners[barrierId] = overlay.owner_worker;
  }

  const produced = interfaceIndex(workerMap, 'interfaces_produced');
  const consumed = interfaceIndex(workerMap, 'interfaces_consumed');
  const aliases = array(addendum?.interface_aliases);
  check(sameSet(aliases.map(alias => alias.id), Object.keys(REQUIRED_ALIASES)), 'both required interface aliases are declared');
  check(aliases.length === new Set(aliases.map(alias => alias.id)).size, 'interface alias IDs are unique');
  for (const [aliasId, expected] of Object.entries(REQUIRED_ALIASES)) {
    const alias = aliases.find(candidate => candidate.id === aliasId);
    check(Boolean(alias), `interface alias exists: ${aliasId}`);
    if (!alias) continue;
    check(alias.producer_worker === expected.producer, `alias producer matches: ${aliasId}`);
    check(alias.source_interface === expected.source && alias.target_interface === expected.target, `alias endpoints match: ${aliasId}`);
    check(sameSet(array(alias.consumer_workers), expected.consumers), `alias consumer closure is exact: ${aliasId}`);
    check(sameSet(produced.get(alias.source_interface) || [], [alias.producer_worker]), `alias source has one producer: ${aliasId}`);
    check(sameSet(consumed.get(alias.target_interface) || [], expected.consumers), `alias target covers every consumer: ${aliasId}`);
    for (const consumer of expected.consumers) check(reaches(alias.producer_worker, consumer), `alias producer precedes consumer: ${aliasId}:${consumer}`);
  }
  const routeAlias = aliases.find(alias => alias.id === 'HlsV2RouteSurface->G-MEDIA-V2');
  check(routeAlias?.kind === 'route-surface' && routeAlias?.preserve_source_order === true, 'G-MEDIA-V2 alias preserves source-ordered routes');

  const contracts = array(addendum?.semantic_contracts);
  check(contracts.length === Object.keys(REQUIRED_CONTRACTS).length, 'semantic contract count is exact');
  check(contracts.length === new Set(contracts.map(contract => contract.interface)).size, 'semantic contract interface IDs are unique');
  check(sameSet(contracts.map(contract => contract.interface), Object.keys(REQUIRED_CONTRACTS)), 'new semantic contract catalog is complete');
  for (const [interfaceName, expected] of Object.entries(REQUIRED_CONTRACTS)) {
    const contract = contracts.find(candidate => candidate.interface === interfaceName);
    check(Boolean(contract), `semantic contract exists: ${interfaceName}`);
    if (!contract) continue;
    check(contract.producer_worker === expected.producer, `semantic producer matches: ${interfaceName}`);
    check(sameSet(array(contract.consumer_workers), expected.consumers), `semantic consumers match: ${interfaceName}`);
    check(sameSet(produced.get(interfaceName) || [], [expected.producer]), `semantic interface has exactly one producer: ${interfaceName}`);
    check(sameSet(consumed.get(interfaceName) || [], expected.consumers), `semantic interface closes every consumer: ${interfaceName}`);
    for (const consumer of expected.consumers) check(reaches(expected.producer, consumer), `semantic producer precedes consumer: ${interfaceName}:${consumer}`);
  }

  const owners = array(addendum?.exclusive_owners);
  check(sameSet(owners.map(owner => owner.id), Object.keys(REQUIRED_OWNERS)), 'all five exclusive ownership records are present');
  check(owners.length === new Set(owners.map(owner => owner.id)).size, 'exclusive owner IDs are unique');
  check(owners.length === new Set(owners.map(owner => owner.owner_worker)).size, 'each new shared scope has one distinct owner');
  const ownedScopePaths = new Set();
  const effectiveOwnersByPath = new Map();
  for (const worker of workerMap.values()) {
    check(array(worker.owned_files).length === new Set(array(worker.owned_files)).size, `effective worker owned paths are unique: ${worker.worker_id}`);
    for (const ownedPath of array(worker.owned_files)) {
      if (!effectiveOwnersByPath.has(ownedPath)) effectiveOwnersByPath.set(ownedPath, []);
      effectiveOwnersByPath.get(ownedPath).push(worker.worker_id);
    }
  }
  for (const [ownerId, expectedWorker] of Object.entries(REQUIRED_OWNERS)) {
    const owner = owners.find(candidate => candidate.id === ownerId);
    check(Boolean(owner), `exclusive owner exists: ${ownerId}`);
    if (!owner) continue;
    check(owner.owner_worker === expectedWorker, `exclusive owner worker matches: ${ownerId}`);
    check(array(owner.paths).length > 0, `exclusive owner path scope is nonempty: ${ownerId}`);
    check(objectSha256(owner) === EXPECTED_OWNER_SHA256[ownerId], `exclusive owner record and path scope are exact: ${ownerId}`);
    const worker = workerMap.get(expectedWorker);
    check(Boolean(worker), `exclusive owner worker exists: ${ownerId}`);
    for (const ownedPath of array(owner.paths)) {
      check(!ownedScopePaths.has(ownedPath), `exclusive ownership scopes do not overlap: ${ownedPath}`);
      ownedScopePaths.add(ownedPath);
      check(array(worker?.owned_files).includes(ownedPath), `owner ledger and worker ownership agree: ${ownerId}:${ownedPath}`);
      const effectiveOwners = ownerId === 'w4-state-checkpoint-mutation' && !W4_CHECKPOINT_PATHS.includes(ownedPath)
        ? array(effectiveOwnersByPath.get(ownedPath)).filter(workerId => workerMap.get(workerId)?.wave === 'W4')
        : array(effectiveOwnersByPath.get(ownedPath));
      check(sameSet(effectiveOwners, [expectedWorker]), `protected path has exactly one effective phase owner: ${ownerId}:${ownedPath}`);
    }
  }
  check(sameSet([...ownedScopePaths], Object.keys(EXPECTED_PROTECTED_PATH_OWNERS)), 'protected path catalog is exact');
  for (const [protectedPath, expectedOwners] of Object.entries(EXPECTED_PROTECTED_PATH_OWNERS)) {
    check(sameSet(effectiveOwnersByPath.get(protectedPath) || [], expectedOwners), `protected path global effective owner set is exact: ${protectedPath}`);
  }
  for (const authorityPath of W4_AUTHORITY_PATHS) {
    const authorityOwners = W4_CHECKPOINT_PATHS.includes(authorityPath)
      ? array(effectiveOwnersByPath.get(authorityPath))
      : array(effectiveOwnersByPath.get(authorityPath)).filter(workerId => workerMap.get(workerId)?.wave === 'W4');
    check(sameSet(authorityOwners, ['INT-W4']), `INT-W4 solely owns W4 state/checkpoint authority: ${authorityPath}`);
  }
  check(workerMap.get('H02-B')?.integration_owner === 'INT-W4', 'INT-W4 is H02-B integration owner');
  check(workerMap.get('E00-B')?.integration_owner === 'INT-W4', 'INT-W4 is E00-B integration owner');
  check(owners.find(owner => owner.id === 'desktop-release-package-mutation')?.owner_worker === 'C02-D', 'release-package mutation owner is C02-D');
  check(owners.find(owner => owner.id === 'assembled-app-torrent-playback-probe')?.owner_worker === 'Q02-P', 'assembled-app probe owner is Q02-P');
  check(reaches('C02-D', 'C02-A') && reaches('C02-D', 'C02-B'), 'C02-D package construction precedes both independent desktop qualification workers');
  check(sameSet(produced.get('WindowsQualificationReceipt') || [], ['C02-A']), 'C02-A alone owns Windows qualification');
  check(sameSet(produced.get('LinuxQualificationReceipt') || [], ['C02-B']), 'C02-B alone owns Linux qualification');

  check(array(workerMap.get('Q04-A')?.interfaces_consumed).includes('RollbackArtifact'), 'Q04 consumes the conditional rollback artifact');
  check(array(produced.get('RollbackArtifact')).length === 0, 'RollbackArtifact has no worker producer and exists only through its conditional virtual gate');
  check(reaches('G-ROLLBACK-ARTIFACT', 'Q04-A'), 'rollback gate precedes Q04');
  const compositionFiles = new Set(array(addendum?.base_invariants?.full_composition_files));
  const compositionOwnerParents = new Set();
  for (const worker of workerMap.values()) {
    for (const ownedFile of array(worker.owned_files)) {
      if (compositionFiles.has(ownedFile)) {
        check(worker.parent_packet === 'C00', `full composition file remains owned only by C00: ${ownedFile}`);
        check(EXPECTED_COMPOSITION_FILE_OWNERS[ownedFile] === worker.worker_id, `full composition file owner is exact: ${ownedFile}`);
        compositionOwnerParents.add(worker.parent_packet);
      }
    }
  }
  check(sameSet([...compositionFiles], Object.keys(EXPECTED_COMPOSITION_FILE_OWNERS)), 'full composition file invariant is exact');
  check(addendum?.base_invariants?.full_composition_parent_packet === 'C00', 'full composition parent invariant is C00');
  check(addendum?.base_invariants?.route_order === 'source-ordered', 'route invariant remains source-ordered');
  check(sameSet(produced.get('G-COMPOSITION') || [], ['C00-C']), 'C00-C remains the sole G-COMPOSITION producer');
  check(!reaches('P01B-A', 'Q04-A'), 'P01B remains optional; Q04 uses the conditional rollback gate');

  if (failures.length) throw new ControlGraphValidationError(failures);
  return {
    schema: 'colosseum-server1-control-graph-verification/v1',
    verdict: failures.length === 0 ? 'VERIFIED' : 'REFUTED',
    checks_passed: pass.length,
    checks_failed: failures.length,
    frozen_authority_inputs_verified: verifiedAuthorityInputs,
    frozen_authority_package_sha256: packageDigest,
    frozen_graph_checks: `${frozenResult.checks_passed}/${frozenResult.checks_passed + frozenResult.checks_failed}`,
    master_packets: packetCount,
    planned_cases: caseIds.length,
    added_workers: addedWorkers.length,
    required_edges: edgeRecords.length,
    interface_aliases: aliases.length,
    semantic_contracts: contracts.length,
    exclusive_owners: owners.length,
    dispatch_stages: dispatchStages.length,
    barrier_owners: barrierOwners,
    rollback_rules: conditionalDependencies.length,
    amended_graph_nodes: dependencies.size,
    amended_graph_acyclic: visited === dependencies.size,
    source_ordered_routes_preserved: routeAlias?.preserve_source_order === true && addendum?.base_invariants?.route_order === 'source-ordered',
    full_composition_owner: compositionOwnerParents.size === 1 ? [...compositionOwnerParents][0] : null,
    release_package_owner: owners.find(owner => owner.id === 'desktop-release-package-mutation')?.owner_worker || null,
    assembled_app_probe_owner: owners.find(owner => owner.id === 'assembled-app-torrent-playback-probe')?.owner_worker || null,
    windows_qualification_owner: (produced.get('WindowsQualificationReceipt') || [])[0] || null,
    linux_qualification_owner: (produced.get('LinuxQualificationReceipt') || [])[0] || null
  };
}

function parseArgs(argv) {
  const values = {};
  for (let index = 0; index < argv.length; index++) {
    if (argv[index] === '--addendum') values.addendum = argv[++index];
    else if (argv[index] === '--frozen-plan-dir') values.frozenPlanDir = argv[++index];
    else throw new Error(`unknown argument: ${argv[index]}`);
  }
  if (!values.addendum || !values.frozenPlanDir) {
    throw new Error('usage: node verify-control-graph.mjs --addendum <json> --frozen-plan-dir <server1-v2.1-parallel>');
  }
  return values;
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : '';
if (invokedPath === fileURLToPath(import.meta.url)) {
  try {
    const args = parseArgs(process.argv.slice(2));
    const basePlan = JSON.parse(fs.readFileSync(path.join(args.frozenPlanDir, 'PARALLEL-WORK-ITEMS.json'), 'utf8'));
    const addendum = JSON.parse(fs.readFileSync(args.addendum, 'utf8'));
    console.log(JSON.stringify(validateControlGraph({ basePlan, addendum, authorityRoot: args.frozenPlanDir }), null, 2));
  } catch (error) {
    const result = {
      schema: 'colosseum-server1-control-graph-verification/v1',
      verdict: 'REFUTED',
      failures: error?.failures || [error.message]
    };
    console.error(JSON.stringify(result, null, 2));
    process.exitCode = 1;
  }
}
