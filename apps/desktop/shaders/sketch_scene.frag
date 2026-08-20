#version 440

layout(std140, binding = 0) uniform Buffer {
    mat4 matrix;
    vec4 color;
    vec4 cameraHigh;
    vec4 cameraLowRotation;
    vec4 viewportPattern;
} ubuf;

layout(location = 0) in float pathDistance;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    float period = ubuf.viewportPattern.w;
    if (period > 0.0) {
        float logicalDistance = pathDistance / ubuf.cameraHigh.z;
        if (mod(logicalDistance, period) > ubuf.viewportPattern.z)
            discard;
    }
    fragmentColor = ubuf.color * ubuf.cameraHigh.w;
}
