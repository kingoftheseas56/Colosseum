const assert = require("assert");
const selector = require("../qml/BiblioLookupSelector.js");

function testPrefersExactAuthorMatch() {
  const results = [
    { title: "The Winds of Winter", author: "John Birmingham" },
    { title: "The Winds of Winter", author: "George R. R. Martin" },
    { title: "A Dream of Spring", author: "George R. R. Martin" },
  ];

  const picked = selector.pickBookMatch(results, "The Winds of Winter", "George R.R. Martin");
  assert.ok(picked, "expected a picked result");
  assert.strictEqual(picked.author, "George R. R. Martin");
}

function testPrefersExactTitleOverLooseMatches() {
  const results = [
    { title: "The Best Military Science Fiction of the 20th Century", author: "George R. R. Martin" },
    { title: "The Winds of Winter", author: "George R. R. Martin" },
  ];

  const picked = selector.pickBookMatch(results, "The Winds of Winter", "George R.R. Martin");
  assert.ok(picked, "expected a picked result");
  assert.strictEqual(picked.title, "The Winds of Winter");
}

function testFallsBackToFirstWhenNoBetterMatchExists() {
  const results = [
    { title: "Dune Messiah", author: "Frank Herbert" },
    { title: "Children of Dune", author: "Frank Herbert" },
  ];

  const picked = selector.pickBookMatch(results, "Dune", "Frank Herbert");
  assert.ok(picked, "expected a picked result");
  assert.strictEqual(picked.title, "Dune Messiah");
}

testPrefersExactAuthorMatch();
testPrefersExactTitleOverLooseMatches();
testFallsBackToFirstWhenNoBetterMatchExists();

console.log("biblio lookup selector tests passed");
