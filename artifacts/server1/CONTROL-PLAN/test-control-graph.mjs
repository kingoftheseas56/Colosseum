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
