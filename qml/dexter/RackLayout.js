.pragma library
// The specimen rack owns these physical dimensions. Pose, grooves and pointer
// projection share them so visual spacing cannot silently drift from hit targets.
var boxLength = 6.6;
var boxWidth = 4.4;
var wallThickness = 0.24;
var rimCenterY = 1.105;
var rimThickness = 0.045;
var rimHeight = rimCenterY + rimThickness / 2;
var slotPitch = 0.46;
var restingCenterY = 1.17;
var slideHeight = 1.25;
var slideWidth = 3.75;
var slideThickness = 0.027;
var pickHeight = restingCenterY + slideHeight / 2;
function firstSlotX(count) { return -(count - 1) * slotPitch / 2; }
function slotX(index, count) { return firstSlotX(count) + index * slotPitch; }
