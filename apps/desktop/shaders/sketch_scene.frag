#version 440

layout(std140, binding = 0) uniform Buffer {
    mat4 matrix;
    vec4 color;
    vec4 cameraHigh;
    vec4 cameraLowRotation;
    vec4 viewportPattern;
} ubuf;

layout(location = 0) in float pathDistance;
layout(location = 1) in vec2 edgeCoverage;
layout(location = 2) flat in vec2 strokePattern;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    float edgeWidth = max(fwidth(edgeCoverage.x), 0.0001);
    float opacity = 1.0 - smoothstep(edgeCoverage.y - edgeWidth * 0.5,
                                    edgeCoverage.y + edgeWidth * 0.5,
                                    abs(edgeCoverage.x));
    float period = strokePattern.y;
    if (period > 0.0) {
        float logicalDistance = pathDistance / ubuf.cameraHigh.z;
        float phase = mod(logicalDistance, period);
        float patternWidth = max(fwidth(logicalDistance) * 0.5, 0.0001);
        float signedStart = phase <= period * 0.5 ? phase : phase - period;
        float startCoverage = smoothstep(-patternWidth * 0.5,
                                         patternWidth * 0.5, signedStart);
        float endCoverage = 1.0 - smoothstep(strokePattern.x - patternWidth,
                                             strokePattern.x, phase);
        opacity *= min(startCoverage, endCoverage);
    }
    if (opacity <= 0.0)
        discard;
    fragmentColor = ubuf.color * (ubuf.cameraHigh.w * opacity);
}
