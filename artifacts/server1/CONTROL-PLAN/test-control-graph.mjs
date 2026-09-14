import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { validateControlGraph } from './verify-control-graph.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(here, '..', '..', '..');
const addendumPath = path.join(repositoryRoot, 'docs', 'server1', 'CONTROL-GRAPH-ADDENDUM.json');
const authorityRoot = process.env.SERVER1_FROZEN_PLAN_DIR;

if (!authorityRoot) {
  throw new Error('SERVER1_FROZEN_PLAN_DIR must name the frozen server1-v2.1-parallel directory');
}

const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const loadFixture = () => ({
  basePlan: readJson(path.join(authorityRoot, 'PARALLEL-WORK-ITEMS.json')),
  addendum: readJson(addendumPath),
  authorityRoot
});
const clone = value => structuredClone(value);
const expectRejected = fixture => assert.throws(
  () => validateControlGraph(fixture),
  error => error?.name === 'ControlGraphValidationError' && error.failures.length > 0
);

const requiredWorkers = ['P08-T', 'H02-C', 'INT-W4', 'C02-D', 'Q02-P'];
const requiredEdges = [
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
const requiredAliases = [
  'HlsV2RouteSurface->G-MEDIA-V2',
  'G-PLATFORM->DesktopPlatformQualification'
];
const requiredOwners = [
  'torrent-transport-public-contract',
  'standalone-torrent-host',
  'w4-state-checkpoint-mutation',
  'desktop-release-package-mutation',
  'assembled-app-torrent-playback-probe'
];
const workerSemanticFields = [
  'worker_id',
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
const barrierOwnerIds = ['B-W4A', 'B-W4B'];
const rollbackEvidence = {
  'buildable-p01b': [
    'pinned source and build identity',
    'successful package build',
    'rollback startup and owned-settings migration test',
    'no deletion of user downloads or cache'
  ],
  'approved-current-sidecar': [
    'pinned current-sidecar source and package identity',
    'explicit Agent 4 approval for this candidate',
    'rollback startup and owned-settings migration test',
    'no deletion of user downloads or cache'
  ]
};

const replaceValue = value => {
  if (Array.isArray(value)) return value.length ? [] : ['MUTATED'];
  if (typeof value === 'boolean') return !value;
  if (typeof value === 'number') return value + 1;
  return 'MUTATED';
};

test('canonical addendum closes the named semantic and ownership gaps', () => {
  const report = validateControlGraph(loadFixture());
  assert.equal(report.verdict, 'VERIFIED');
  assert.equal(report.frozen_authority_inputs_verified, 9);
  assert.equal(report.master_packets, 64);
  assert.equal(report.planned_cases, 193);
  assert.equal(report.added_workers, 5);
  assert.equal(report.required_edges, requiredEdges.length);
  assert.equal(report.interface_aliases, requiredAliases.length);
  assert.equal(report.exclusive_owners, requiredOwners.length);
  assert.equal(report.rollback_rules, 1);
});

test('removing any required worker is rejected', async t => {
  for (const workerId of requiredWorkers) {
    await t.test(workerId, () => {
      const fixture = loadFixture();
      fixture.addendum.workers = fixture.addendum.workers.filter(worker => worker.worker_id !== workerId);
      expectRejected(fixture);
    });
  }
});

test('removing any required dependency or dispatch edge is rejected', async t => {
  for (const edgeId of requiredEdges) {
    await t.test(edgeId, () => {
      const fixture = loadFixture();
      fixture.addendum.required_edges = fixture.addendum.required_edges.filter(edge => edge.id !== edgeId);
      expectRejected(fixture);
    });
  }
});

test('removing either route or platform alias is rejected', async t => {
  for (const aliasId of requiredAliases) {
    await t.test(aliasId, () => {
      const fixture = loadFixture();
      fixture.addendum.interface_aliases = fixture.addendum.interface_aliases.filter(alias => alias.id !== aliasId);
      expectRejected(fixture);
    });
  }
});

test('removing any exclusive owner is rejected', async t => {
  for (const ownerId of requiredOwners) {
    await t.test(ownerId, () => {
      const fixture = loadFixture();
      fixture.addendum.exclusive_owners = fixture.addendum.exclusive_owners.filter(owner => owner.id !== ownerId);
      expectRejected(fixture);
    });
  }
});

test('removing the conditional rollback rule is rejected', () => {
  const fixture = loadFixture();
  fixture.addendum.conditional_dependencies = [];
  expectRejected(fixture);
});

test('authority digest drift is rejected before graph acceptance', () => {
  const fixture = loadFixture();
  fixture.addendum.frozen_authority.inputs[0].sha256 = '0'.repeat(64);
  expectRejected(fixture);
});

test('deleting or replacing every required worker semantic field is rejected independently', async t => {
  for (const workerId of requiredWorkers) {
    for (const field of workerSemanticFields) {
      await t.test(`${workerId}:${field}:delete`, () => {
        const fixture = loadFixture();
        const worker = fixture.addendum.workers.find(candidate => candidate.worker_id === workerId);
        delete worker[field];
        expectRejected(fixture);
      });
      await t.test(`${workerId}:${field}:replace`, () => {
        const fixture = loadFixture();
        const worker = fixture.addendum.workers.find(candidate => candidate.worker_id === workerId);
        worker[field] = replaceValue(worker[field]);
        expectRejected(fixture);
      });
    }
  }
});

test('exclusive owner path scopes must be nonempty and exact', async t => {
  for (const ownerId of requiredOwners) {
    await t.test(`${ownerId}:empty`, () => {
      const fixture = loadFixture();
      fixture.addendum.exclusive_owners.find(owner => owner.id === ownerId).paths = [];
      expectRejected(fixture);
    });
    await t.test(`${ownerId}:replace`, () => {
      const fixture = loadFixture();
      fixture.addendum.exclusive_owners.find(owner => owner.id === ownerId).paths[0] = 'MUTATED';
      expectRejected(fixture);
    });
    await t.test(`${ownerId}:append`, () => {
      const fixture = loadFixture();
      fixture.addendum.exclusive_owners.find(owner => owner.id === ownerId).paths.push('MUTATED');
      expectRejected(fixture);
    });
    for (const field of ['id', 'owner_worker', 'paths', 'phase', 'supersedes_worker']) {
      await t.test(`${ownerId}:${field}:delete`, () => {
        const fixture = loadFixture();
        const owner = fixture.addendum.exclusive_owners.find(candidate => candidate.id === ownerId);
        delete owner[field];
        expectRejected(fixture);
      });
      await t.test(`${ownerId}:${field}:replace`, () => {
        const fixture = loadFixture();
        const owner = fixture.addendum.exclusive_owners.find(candidate => candidate.id === ownerId);
        owner[field] = replaceValue(owner[field]);
        expectRejected(fixture);
      });
    }
    const paths = loadFixture().addendum.exclusive_owners.find(owner => owner.id === ownerId).paths;
    for (let index = 0; index < paths.length; index++) {
      await t.test(`${ownerId}:remove-path:${index}`, () => {
        const fixture = loadFixture();
        fixture.addendum.exclusive_owners.find(owner => owner.id === ownerId).paths.splice(index, 1);
        expectRejected(fixture);
      });
      await t.test(`${ownerId}:replace-path:${index}`, () => {
        const fixture = loadFixture();
        fixture.addendum.exclusive_owners.find(owner => owner.id === ownerId).paths[index] = 'MUTATED';
        expectRejected(fixture);
      });
    }
  }
});

test('every dispatch stage and its exact ordering are mandatory', async t => {
  const canonical = loadFixture().addendum.dispatch_overrides;
  for (const wave of canonical) {
    for (const field of ['wave', 'stages']) {
      await t.test(`${wave.wave}:${field}:delete`, () => {
        const fixture = loadFixture();
        const override = fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave);
        delete override[field];
        expectRejected(fixture);
      });
      await t.test(`${wave.wave}:${field}:replace`, () => {
        const fixture = loadFixture();
        const override = fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave);
        override[field] = replaceValue(override[field]);
        expectRejected(fixture);
      });
    }
    for (let index = 0; index < wave.stages.length; index++) {
      await t.test(`${wave.wave}:remove:${index}`, () => {
        const fixture = loadFixture();
        fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave).stages.splice(index, 1);
        expectRejected(fixture);
      });
      await t.test(`${wave.wave}:replace:${index}`, () => {
        const fixture = loadFixture();
        fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave).stages[index] = {
          id: 'MUTATED', after: [], release: []
        };
        expectRejected(fixture);
      });
      for (const field of ['id', 'after', 'release']) {
        await t.test(`${wave.wave}:${index}:${field}:delete`, () => {
          const fixture = loadFixture();
          const stage = fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave).stages[index];
          delete stage[field];
          expectRejected(fixture);
        });
        await t.test(`${wave.wave}:${index}:${field}:replace`, () => {
          const fixture = loadFixture();
          const stage = fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave).stages[index];
          stage[field] = replaceValue(stage[field]);
          expectRejected(fixture);
        });
      }
    }
    if (wave.stages.length > 1) {
      await t.test(`${wave.wave}:order`, () => {
        const fixture = loadFixture();
        const stages = fixture.addendum.dispatch_overrides.find(entry => entry.wave === wave.wave).stages;
        [stages[0], stages[1]] = [stages[1], stages[0]];
        expectRejected(fixture);
      });
    }
  }
});

test('both W4 barrier-owner overlays and every semantic field are mandatory', async t => {
  for (const barrierId of barrierOwnerIds) {
    await t.test(`${barrierId}:remove`, () => {
      const fixture = loadFixture();
      fixture.addendum.barrier_owner_overrides = (fixture.addendum.barrier_owner_overrides || [])
        .filter(entry => entry.barrier_id !== barrierId);
      expectRejected(fixture);
    });
    for (const field of ['barrier_id', 'owner_worker', 'producer_worker', 'checkpoint_path', 'state_path', 'supersedes_worker', 'ownership']) {
      await t.test(`${barrierId}:${field}:delete`, () => {
        const fixture = loadFixture();
        const overlay = (fixture.addendum.barrier_owner_overrides || [])
          .find(entry => entry.barrier_id === barrierId);
        if (overlay) delete overlay[field];
        expectRejected(fixture);
      });
      await t.test(`${barrierId}:${field}:replace`, () => {
        const fixture = loadFixture();
        const overlay = (fixture.addendum.barrier_owner_overrides || [])
          .find(entry => entry.barrier_id === barrierId);
        if (overlay) overlay[field] = 'MUTATED';
        expectRejected(fixture);
      });
    }
  }
});

test('every rollback evidence requirement is exact and mandatory', async t => {
  for (const [optionId, requirements] of Object.entries(rollbackEvidence)) {
    for (let index = 0; index < requirements.length; index++) {
      await t.test(`${optionId}:remove:${index}`, () => {
        const fixture = loadFixture();
        const option = fixture.addendum.conditional_dependencies[0].options.find(candidate => candidate.id === optionId);
        option.required_evidence.splice(index, 1);
        expectRejected(fixture);
      });
      await t.test(`${optionId}:replace:${index}`, () => {
        const fixture = loadFixture();
        const option = fixture.addendum.conditional_dependencies[0].options.find(candidate => candidate.id === optionId);
        option.required_evidence[index] = 'MUTATED';
        expectRejected(fixture);
      });
    }
  }
});

test('every rollback rule and option field is exact', async t => {
  for (const field of ['consumer_worker', 'virtual_gate', 'produces_interface', 'selection', 'options']) {
    await t.test(`${field}:delete`, () => {
      const fixture = loadFixture();
      delete fixture.addendum.conditional_dependencies[0][field];
      expectRejected(fixture);
    });
    await t.test(`${field}:replace`, () => {
      const fixture = loadFixture();
      const rule = fixture.addendum.conditional_dependencies[0];
      rule[field] = replaceValue(rule[field]);
      expectRejected(fixture);
    });
  }
  for (const optionId of Object.keys(rollbackEvidence)) {
    for (const field of ['id', 'source_worker', 'required_evidence']) {
      await t.test(`${optionId}:${field}:delete`, () => {
        const fixture = loadFixture();
        const option = fixture.addendum.conditional_dependencies[0].options.find(candidate => candidate.id === optionId);
        delete option[field];
        expectRejected(fixture);
      });
      await t.test(`${optionId}:${field}:replace`, () => {
        const fixture = loadFixture();
        const option = fixture.addendum.conditional_dependencies[0].options.find(candidate => candidate.id === optionId);
        option[field] = replaceValue(option[field]);
        expectRejected(fixture);
      });
    }
  }
});

test('every base composition and route invariant is exact', async t => {
  for (const field of [
    'master_packets',
    'planned_cases',
    'master_packet_outcomes_changed',
    'case_assignments_changed',
    'frozen_public_contracts_changed',
    'full_composition_parent_packet',
    'full_composition_files',
    'route_order'
  ]) {
    await t.test(`${field}:delete`, () => {
      const fixture = loadFixture();
      delete fixture.addendum.base_invariants[field];
      expectRejected(fixture);
    });
    await t.test(`${field}:replace`, () => {
      const fixture = loadFixture();
      fixture.addendum.base_invariants[field] = replaceValue(fixture.addendum.base_invariants[field]);
      expectRejected(fixture);
    });
  }
  const compositionFiles = loadFixture().addendum.base_invariants.full_composition_files;
  for (let index = 0; index < compositionFiles.length; index++) {
    await t.test(`full_composition_files:remove:${index}`, () => {
      const fixture = loadFixture();
      fixture.addendum.base_invariants.full_composition_files.splice(index, 1);
      expectRejected(fixture);
    });
    await t.test(`full_composition_files:replace:${index}`, () => {
      const fixture = loadFixture();
      fixture.addendum.base_invariants.full_composition_files[index] = 'MUTATED';
      expectRejected(fixture);
    });
  }
});

test('digest algorithm description cannot be deleted', () => {
  const fixture = loadFixture();
  delete fixture.addendum.frozen_authority.manifest_algorithm;
  expectRejected(fixture);
});

test('digest algorithm description cannot be replaced', () => {
  const fixture = loadFixture();
  fixture.addendum.frozen_authority.manifest_algorithm = 'MUTATED';
  expectRejected(fixture);
});
