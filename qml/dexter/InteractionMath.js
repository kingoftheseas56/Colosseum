.pragma library
.import "RackLayout.js" as Rack
// Rack picking and glass projection use the same scene units as RackLayout.
var count=13, pitch=Rack.slotPitch, firstX=Rack.firstSlotX(count), topY=Rack.pickHeight;
function clamp(v,a,b){return Math.max(a,Math.min(b,v))}
function smooth(v){var t=clamp(v,0,1);return t*t*(3-2*t)}
function mix(a,b,t){return a+(b-a)*t}
function add(a,b){return [a[0]+b[0],a[1]+b[1],a[2]+b[2]]}
function scale(a,k){return [a[0]*k,a[1]*k,a[2]*k]}
function sub(a,b){return add(a,scale(b,-1))}
function dot(a,b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]}
function cross(a,b){return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]}
function norm(a){return scale(a,1/Math.sqrt(dot(a,a)))}
function camera(w,h){var fit=Math.max(1,1.38/(w/h));var pos=[-12*fit,.8+7.7*fit,6*fit],forward=norm(sub([.8,.8,0],pos)),right=norm(cross(forward,[0,1,0])),up=cross(right,forward);return {pos:pos,forward:forward,right:right,up:up,focal:h/(2*Math.tan(17*Math.PI/180)),width:w,height:h}}
function project(point,c){var d=sub(point,c.pos),z=dot(d,c.forward);return {x:c.width/2+c.focal*dot(d,c.right)/z,y:c.height/2-c.focal*dot(d,c.up)/z}}
function pointerRay(x, y, c) {
    return add(c.forward, add(scale(c.right, (x - c.width / 2) / c.focal),
                             scale(c.up, (c.height / 2 - y) / c.focal)))
}
function planePoint(x,y,c){var ray=pointerRay(x,y,c);if(Math.abs(ray[1])<1e-9)return null;var t=(topY-c.pos[1])/ray[1];return t>0?add(c.pos,scale(ray,t)):null}
function restingGlassHit(x, y, c, selected, lifts) {
    var ray = pointerRay(x, y, c)
    if (ray[0] <= 0) return -1

    // Check the near side of the rim before picking anything through the wood.
    var front = (-Rack.boxLength / 2 + Rack.wallThickness - c.pos[0]) / ray[0]
    var side = ray[2] < 0 ? (Rack.boxWidth / 2 - Rack.wallThickness - c.pos[2]) / ray[2] : Infinity
    var entry = Math.max(front, side)
    if (c.pos[1] + entry * ray[1] < Rack.rimHeight || !isFinite(entry)) return -1

    // From this camera the lower-numbered exposed glass is in front.
    // Use the rack pose for specimens returning to their grooves so the next
    // pickup can interrupt their animation.
    for (var i = 0; i < count; ++i) {
        if (i === selected) continue
        var faceX = Rack.slotX(i, count) - Rack.slideThickness / 2
        var t = (faceX - c.pos[0]) / ray[0]
        var yOnGlass = c.pos[1] + t * ray[1]
        var zOnGlass = c.pos[2] + t * ray[2]
        var lift = lifts[i] || 0
        if (yOnGlass >= Rack.restingCenterY - Rack.slideHeight / 2 + lift &&
            yOnGlass <= topY + lift + .015 && Math.abs(zOnGlass) <= Rack.slideWidth / 2)
            return i
    }
    return -1
}
function overBox(x, y, c) {
    // A little space around the rack lets a near miss still seat the glass.
    var padding = .18
    var ray = pointerRay(x, y, c)
    var min = [-Rack.boxLength / 2 - padding, -padding, -Rack.boxWidth / 2 - padding]
    var max = [Rack.boxLength / 2 + padding, topY + padding, Rack.boxWidth / 2 + padding]
    var near = 0
    var far = Infinity
    for (var axis = 0; axis < 3; ++axis) {
        if (Math.abs(ray[axis]) < 1e-9) {
            if (c.pos[axis] < min[axis] || c.pos[axis] > max[axis]) return false
            continue
        }
        var a = (min[axis] - c.pos[axis]) / ray[axis]
        var b = (max[axis] - c.pos[axis]) / ray[axis]
        near = Math.max(near, Math.min(a, b))
        far = Math.min(far, Math.max(a, b))
    }
    return far >= near
}
// Quaternions use [w,x,y,z], matching QML's Qt.quaternion convention.
function multiply(a,b){return [a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]]}
function cameraQuaternion(c){var r=c.right,u=c.up,b=scale(c.forward,-1),m00=r[0],m01=u[0],m02=b[0],m10=r[1],m11=u[1],m12=b[1],m20=r[2],m21=u[2],m22=b[2],tr=m00+m11+m22,s;
 if(tr>0){s=.5/Math.sqrt(tr+1);return [.25/s,(m21-m12)*s,(m02-m20)*s,(m10-m01)*s]}
 if(m00>m11&&m00>m22){s=2*Math.sqrt(1+m00-m11-m22);return [(m21-m12)/s,.25*s,(m01+m10)/s,(m02+m20)/s]}
 if(m11>m22){s=2*Math.sqrt(1+m11-m00-m22);return [(m02-m20)/s,(m01+m10)/s,.25*s,(m12+m21)/s]}
 s=2*Math.sqrt(1+m22-m00-m11);return [(m10-m01)/s,(m02+m20)/s,(m12+m21)/s,.25*s]
}
function slerp(a,b,t){var cos=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];if(cos<0){b=b.map(function(v){return-v});cos=-cos}var k0,k1;if(cos>.9995){k0=1-t;k1=t}else{var theta=Math.acos(clamp(cos,-1,1)),sin=Math.sin(theta);k0=Math.sin((1-t)*theta)/sin;k1=Math.sin(t*theta)/sin}var q=a.map(function(v,i){return k0*v+k1*b[i]}),n=Math.sqrt(q.reduce(function(sum,v){return sum+v*v},0));return q.map(function(v){return v/n})}
function rotate(v,q){var p=multiply(multiply(q,[0,v[0],v[1],v[2]]),[q[0],-q[1],-q[2],-q[3]]);return[p[1],p[2],p[3]]}
function pose(index,p,c){var x=firstX+index*pitch,lift=smooth(p/.42),turn=smooth((p-.24)/.72),travel=smooth((p-.31)/.69),t=(2.8-c.pos[1])/(topY-c.pos[1]);var position=[mix(x,c.pos[0]+t*(x-c.pos[0]),travel),mix(Rack.restingCenterY+1.65*lift,2.8,travel),(c.pos[2]+t*(-3.4-c.pos[2]))*travel];var half=Math.SQRT1_2,rotation=slerp([half,0,-half,0],multiply(cameraQuaternion(c),[half,0,0,half]),turn);return {position:position,rotation:rotation}}
function glassCenter(index, progress, c, lift, offset) {
    var center = pose(index, progress, c).position
    center[1] += lift
    return offset ? add(center, offset) : center
}
function corners(index, p, c, lift, offset) {
    var center = glassCenter(index, p, c, lift || 0, offset)
    var rotation = pose(index, p, c).rotation
    return [[-1,-1],[1,-1],[1,1],[-1,1]].map(function(pt) {
        return project(add(rotate([pt[0] * Rack.slideWidth / 2, pt[1] * Rack.slideHeight / 2, Rack.slideThickness / 2], rotation), center), c)
    })
}
function offsetToPoint(center, x, y, c) {
    var screen = project(center, c)
    var unitsPerPixel = dot(sub(center, c.pos), c.forward) / c.focal
    return add(scale(c.right, (x - screen.x) * unitsPerPixel),
               scale(c.up, (screen.y - y) * unitsPerPixel))
}
function hitTest(x, y, c) {
    var hit = planePoint(x, y, c)
    if (!hit) return { index: -1, coordinate: null, withinRail: false }
    var coordinate = (hit[0] - firstX) / pitch
    var inside = coordinate >= -.5 && coordinate <= count - .5 &&
                 Math.abs(hit[2]) <= Rack.slideWidth / 2
    return { index: inside ? clamp(Math.floor(coordinate + .5), 0, count - 1) : -1,
             coordinate: coordinate, withinRail: inside }
}
function slotPoint(index, c) {
    return project([Rack.slotX(index, count), topY, 0], c)
}
function inPolygon(pts,x,y){var sign=0;for(var i=0;i<pts.length;i++){var a=pts[i],b=pts[(i+1)%pts.length],cross=(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);if(Math.abs(cross)<.001)continue;var next=Math.sign(cross);if(sign&&next!==sign)return false;sign=next}return true}
