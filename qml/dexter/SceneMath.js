.pragma library
.import "RackLayout.js" as Rack

// Same right-handed coordinates as the approved Three.js scene. Scene dimensions
// are multiplied by 100 for Qt Quick 3D's built-in primitive convention.
function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function smooth(v) { var t=clamp(v,0,1); return t*t*(3-2*t); }
function mix(a,b,t) { return a+(b-a)*t; }
function vector(x,y,z) { return Qt.vector3d(x,y,z); }
function normalize(a) { var n=Math.sqrt(a.x*a.x+a.y*a.y+a.z*a.z); return vector(a.x/n,a.y/n,a.z/n); }
function cross(a,b) { return vector(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
function multiply(a,b) {
    return Qt.quaternion(a.scalar*b.scalar-a.x*b.x-a.y*b.y-a.z*b.z,
                         a.scalar*b.x+a.x*b.scalar+a.y*b.z-a.z*b.y,
                         a.scalar*b.y-a.x*b.z+a.y*b.scalar+a.z*b.x,
                         a.scalar*b.z+a.x*b.y-a.y*b.x+a.z*b.scalar);
}
function slerp(a,b,t) {
    var w=b.scalar, x=b.x, y=b.y, z=b.z;
    var d=a.scalar*w+a.x*x+a.y*y+a.z*z;
    if(d<0) { d=-d; w=-w; x=-x; y=-y; z=-z; }
    var u=1-t, v=t;
    if(d<0.9995) { var angle=Math.acos(clamp(d,-1,1)), s=Math.sin(angle);u=Math.sin((1-t)*angle)/s;v=Math.sin(t*angle)/s; }
    var qw=a.scalar*u+w*v, qx=a.x*u+x*v, qy=a.y*u+y*v, qz=a.z*u+z*v;
    var len=Math.sqrt(qw*qw+qx*qx+qy*qy+qz*qz);
    return Qt.quaternion(qw/len,qx/len,qy/len,qz/len);
}
function lookAt(eye,target) {
    var z=normalize(vector(eye.x-target.x,eye.y-target.y,eye.z-target.z));
    var x=normalize(cross(vector(0,1,0),z)), y=cross(z,x);
    var m00=x.x,m01=y.x,m02=z.x,m10=x.y,m11=y.y,m12=z.y,m20=x.z,m21=y.z,m22=z.z;
    var trace=m00+m11+m22,w,qx,qy,qz,s;
    if(trace>0) { s=.5/Math.sqrt(trace+1);w=.25/s;qx=(m21-m12)*s;qy=(m02-m20)*s;qz=(m10-m01)*s; }
    else if(m00>m11 && m00>m22) { s=2*Math.sqrt(1+m00-m11-m22);w=(m21-m12)/s;qx=.25*s;qy=(m01+m10)/s;qz=(m02+m20)/s; }
    else if(m11>m22) { s=2*Math.sqrt(1+m11-m00-m22);w=(m02-m20)/s;qx=(m01+m10)/s;qy=.25*s;qz=(m12+m21)/s; }
    else { s=2*Math.sqrt(1+m22-m00-m11);w=(m10-m01)/s;qx=(m02+m20)/s;qy=(m12+m21)/s;qz=.25*s; }
    return Qt.quaternion(w,qx,qy,qz);
}
function pose(index,count,progress,camera) {
    var x=Rack.slotX(index,count)*100;
    var lift=smooth(progress/.42), turn=smooth((progress-.24)/.72), travel=smooth((progress-.31)/.69);
    var rayT=(280-camera.y)/(Rack.pickHeight*100-camera.y);
    return {position:vector(mix(x,camera.x+rayT*(x-camera.x),travel),mix(Rack.restingCenterY*100+165*lift,280,travel),(camera.z+rayT*(-340-camera.z))*travel),turn:turn,travel:travel};
}
// A two-step beveled extrusion matching cube() in scene-hover.js. UVs keep
// one continuous lengthwise wood-fibre scale independent of each board's size.
function beveledBox(w,h,d,surface) {
    var b=Math.min(.018,w*.07,h*.07,d*.07), k=Math.SQRT1_2;
    var rings=[[-d/2,w/2-b,h/2-b],[-d/2+b*(1-k),w/2-b+b*k,h/2-b+b*k],[-d/2+b,w/2,h/2],[d/2-b,w/2,h/2],[d/2-b*(1-k),w/2-b+b*k,h/2-b+b*k],[d/2,w/2-b,h/2-b]];
    var verts=[],norms=[],uvs=[],tangents=[],binormals=[],indices=[];
    function point(r,i) { return vector((i===0||i===3)?-r[1]:r[1],i<2?-r[2]:r[2],r[0]); }
    function face(a,c,e,f) {
        var n=normalize(cross(vector(c.x-a.x,c.y-a.y,c.z-a.z),vector(e.x-a.x,e.y-a.y,e.z-a.z)));
        var ps=[a,c,e,f], start=verts.length;
        for(var j=0;j<4;j++) {
            var p=ps[j];verts.push(vector(p.x*100,p.y*100,p.z*100));norms.push(n);
            if(surface === "lining") {
                // Unlike lengthwise wood fibres, a lining swatch must map once
                // per face; repeating its edge created stripes across the lid.
                var ax=Math.abs(n.x),ay=Math.abs(n.y),az=Math.abs(n.z);
                var u=ay>=ax && ay>=az ? p.x/w+.5 : ax>=az ? p.z/d+.5 : p.x/w+.5;
                var v=ay>=ax && ay>=az ? p.z/d+.5 : p.y/h+.5;
                uvs.push(Qt.vector2d(clamp(u,0,1),clamp(v,0,1)));
            } else {
                var longitudinal=w>=d?p.x:p.z;
                var crosswise=Math.abs(n.y)>.5?(w>=d?p.z:p.x):p.y;
                uvs.push(Qt.vector2d(longitudinal/8+.5,crosswise/1.3+.5));
            }
        }
        // The original box UV projection intentionally collapses U on end
        // faces. Three.js bump derivatives tolerate this, but native normal
        // mapping requires a finite tangent frame. Preserve the original UVs
        // and supply a safe orthonormal frame instead of dividing by zero.
        var u0=uvs[start],u1=uvs[start+1],u2=uvs[start+2];
        var ex1=c.x-a.x,ey1=c.y-a.y,ez1=c.z-a.z;
        var ex2=e.x-a.x,ey2=e.y-a.y,ez2=e.z-a.z;
        var du1=u1.x-u0.x,dv1=u1.y-u0.y,du2=u2.x-u0.x,dv2=u2.y-u0.y;
        var determinant=du1*dv2-du2*dv1, tangent,binormal;
        if(Math.abs(determinant)>1e-12) {
            tangent=normalize(vector((ex1*dv2-ex2*dv1)/determinant,(ey1*dv2-ey2*dv1)/determinant,(ez1*dv2-ez2*dv1)/determinant));
            binormal=normalize(vector((ex2*du1-ex1*du2)/determinant,(ey2*du1-ey1*du2)/determinant,(ez2*du1-ez1*du2)/determinant));
        } else {
            // The source's crosswise coordinate on endgrain is world Y.
            var ref=Math.abs(n.y)<.95?vector(0,1,0):vector(0,0,1);
            tangent=normalize(cross(ref,n));
            binormal=normalize(cross(n,tangent));
        }
        for(var k=0;k<4;k++){tangents.push(tangent);binormals.push(binormal);}
        indices.push(start,start+1,start+2,start,start+2,start+3);
    }
    face(point(rings[0],3),point(rings[0],2),point(rings[0],1),point(rings[0],0));
    face(point(rings[5],0),point(rings[5],1),point(rings[5],2),point(rings[5],3));
    for(var r=0;r<rings.length-1;r++)for(var i=0;i<4;i++)face(point(rings[r],i),point(rings[r],(i+1)%4),point(rings[r+1],(i+1)%4),point(rings[r+1],i));
    return {positions:verts,normals:norms,uvs:uvs,tangents:tangents,binormals:binormals,indexes:indices};
}
