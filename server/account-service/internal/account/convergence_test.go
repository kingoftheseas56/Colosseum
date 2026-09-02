package account

import (
	"reflect"
	"runtime"
	"strings"
	"testing"
)

// TestArc36ServerConvergence groups the already isolated PostgreSQL proofs into
// the final acceptance matrix. Each delegated test creates and resets its own
// _test database state, so a failure remains attributable to one contract row.
// The delegated assertions are the failure construction: changing canonical
// state, attachment ownership, or cursor boundaries makes the corresponding
// focused test fail rather than merely checking that a request returned.
func TestArc36ServerConvergence(t *testing.T) {
	matrix := []struct {
		name  string
		tests []func(*testing.T)
	}{
		{
			name: "semantic-history-merge",
			tests: []func(*testing.T){
				TestSyncHistorySemanticMergeAdvancesCanonicalSeq,
				TestSyncHistoryNoContributionKeepsCanonicalSeq,
				TestSyncMergeHistoryCompletionAndMetadata,
			},
		},
		{
			name: "tombstones-and-resurrection",
			tests: []func(*testing.T){
				TestSyncWinnerIgnoresArrivalOrderAndTombstoneIsFirstClass,
				TestSyncCanonicalTombstoneAndResurrection,
			},
		},
		{
			name: "activity-fact-convergence",
			tests: []func(*testing.T){
				TestActivityFactTransportRetryIsIdempotent,
				TestActivityFactSemanticDuplicateKeepsOneFactWithoutSequencing,
				TestActivityFactConflictingContentIsRejected,
				TestActivityFactCrossDeviceDuplicateRemainsOneFact,
			},
		},
		{
			name: "attachment-lifecycle",
			tests: []func(*testing.T){
				TestBeginProfileAttachmentFreezesCommittedBaseline,
				TestCommitProfileAttachmentIdempotentLifecycle,
				TestAttachedPushTagsRowsWithoutChangingMutationIdentity,
				TestAttachedPushRejectsForeignOrTerminalAttachments,
			},
		},
		{
			name: "snapshot-pagination",
			tests: []func(*testing.T){
				TestSyncSnapshotFirstPageFreezesCursorAcrossPages,
				TestSyncCanonicalPullPaginatesCurrentRowsAscending,
				TestSyncUnifiedPullPaginatesMixedStream,
			},
		},
		{
			name: "account-isolation",
			tests: []func(*testing.T){
				TestSyncSnapshotAccountIsolationAndTokenRejection,
				TestAttachedPushRejectsForeignOrTerminalAttachments,
			},
		},
		{
			name: "lost-response-retry",
			tests: []func(*testing.T){
				TestSyncDuplicateMutationIsIdempotent,
				TestSyncHistorySemanticRetryIsIdempotent,
				TestActivityFactTransportRetryIsIdempotent,
				TestCommitProfileAttachmentIdempotentLifecycle,
			},
		},
	}

	for _, row := range matrix {
		row := row
		t.Run(row.name, func(t *testing.T) {
			for _, proof := range row.tests {
				proof := proof
				t.Run(testName(proof), proof)
			}
		})
	}
}

func testName(proof func(*testing.T)) string {
	name := runtime.FuncForPC(reflect.ValueOf(proof).Pointer()).Name()
	parts := strings.Split(name, ".")
	return parts[len(parts)-1]
}
