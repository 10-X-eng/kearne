#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 positionLow;
layout(location = 2) in vec2 extrusion;
layout(location = 3) in float pathDistanceMetres;

layout(std140, binding = 0) uniform Buffer {
    mat4 matrix;
    vec4 color;
    vec4 cameraHigh;
    vec4 cameraLowRotation;
    vec4 viewportPattern;
} ubuf;

layout(location = 0) out float pathDistance;

void main()
{
    vec2 relative = (position - ubuf.cameraHigh.xy)
                  + (positionLow - ubuf.cameraLowRotation.xy);
    float cosine = ubuf.cameraLowRotation.z;
    float sine = ubuf.cameraLowRotation.w;
    vec2 item = ubuf.viewportPattern.xy * 0.5;
    item += vec2(cosine * relative.x - sine * relative.y,
                -(sine * relative.x + cosine * relative.y))
          / ubuf.cameraHigh.z;
    item += vec2(cosine * extrusion.x - sine * extrusion.y,
                -(sine * extrusion.x + cosine * extrusion.y));
    gl_Position = ubuf.matrix * vec4(item, 0.0, 1.0);
    pathDistance = pathDistanceMetres;
}
