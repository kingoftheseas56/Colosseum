import QtQuick
import QtTest
import "../qml/PosterSourcePolicy.js" as Policy
Item {
 TestCase {
  name: "PosterOriginFallback"
  readonly property string proxy: "https://i0.wp.com/getcomics.org/share/uploads/2026/01/Cover%20One.jpg?fit=400%2C615&ssl=1"
  readonly property string origin: "https://getcomics.org/share/uploads/2026/01/Cover%20One.jpg"
  function test_proxy_then_origin() {
   compare(Policy.candidates(proxy, []).join("|"), proxy+"|"+origin)
   compare(Policy.getComicsOriginCandidate(proxy),origin)
   compare(Policy.getComicsOriginCandidate("http://i0.wp.com/getcomics.org/share/uploads/A.jpg"),"https://getcomics.org/share/uploads/A.jpg")
  }
  function test_owned_then_proxy_then_origin() {
   var owned="https://covers.example.org/covers/owned.webp"
   compare(Policy.candidates(owned,[proxy,origin,proxy]).join("|"),owned+"|"+proxy+"|"+origin)
  }
  function test_origin_inserted_before_other_explicit_fallback() {
   compare(Policy.candidates(proxy,["https://other.example.org/art.jpg",origin]).join("|"),proxy+"|"+origin+"|https://other.example.org/art.jpg")
  }
  function test_foreign_and_metahub_unchanged() {
   compare(Policy.candidates("https://foreign.example.org/art.jpg",[]).join("|"),"https://foreign.example.org/art.jpg")
   compare(Policy.candidates("https://images.metahub.space/poster/medium/tt1/img",[]).join("|"),"https://live.metahub.space/poster/small/tt1/img")
  }
  function test_unsafe_inputs() {
   var values=[
    "https://i0.wp.com.evil/getcomics.org/share/uploads/A.jpg",
    "https://evil.i0.wp.com/getcomics.org/share/uploads/A.jpg",
    "https://user@i0.wp.com/getcomics.org/share/uploads/A.jpg",
    "https://i0.wp.com:8443/getcomics.org/share/uploads/A.jpg",
    "https://i0.wp.com/getcomics.org.evil/share/uploads/A.jpg",
    "https://i0.wp.com/evil.org/share/uploads/A.jpg",
    "https://i0.wp.com/getcomics.org/wp-json/posts",
    "https://i0.wp.com/getcomics.org/share/uploads/../secret.jpg",
    "https://i0.wp.com/getcomics.org/share/uploads/%2e%2e/secret.jpg",
    "https://i0.wp.com/getcomics.org/share/uploads/a%2f..%2fsecret.jpg",
    "https://i0.wp.com/getcomics.org/share/uploads/a%5csecret.jpg",
    "https://i0.wp.com/getcomics.org/share/uploads/a%0asecret.jpg",
    "https://i0.wp.com/getcomics.org/share/uploads/A.jpg#fragment",
    "https://i0.wp.com/getcomics.org/share/uploads/A.jpg?token=secret",
    "https://i0.wp.com/getcomics.org/share/uploads/A.jpg?fit=bad&ssl=1",
    "https://i0.wp.com/getcomics.org/share/uploads/A.jpg?fit=400%2C615&fit=1%2C2",
    "https://i0.wp.com/getcomics.org/share/uploads/A.jpg?ssl=%",
    "https://i0.wp.com/getcomics.org/share/uploads/",
    "file:///getcomics.org/share/uploads/A.jpg", "", null, undefined
   ]
   for(var i=0;i<values.length;i++)compare(Policy.getComicsOriginCandidate(values[i]),"",String(values[i]))
  }
 }
}
