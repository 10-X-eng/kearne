#version 440

struct VectorRecord {
  uvec4 meta;
  vec4 boundsMinimum;
  vec4 boundsMaximum;
  vec4 first;
  vec4 second;
  vec4 shape;
  vec4 domain;
  vec4 appearance;
};

layout(std140, binding = 0) uniform Buffer {
  mat4 matrix;
  vec4 color;
  vec4 cameraHigh;
  vec4 cameraLowRotation;
  vec4 viewport;
}
ubuf;

layout(std430, binding = 1) readonly buffer Records { VectorRecord records[]; };

layout(location = 0) out vec2 queryRelativeMetres;
layout(location = 1) flat out uint recordIndex;

void main() {
  const vec2 corners[6] =
      vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 0.0),
              vec2(1.0, 1.0), vec2(0.0, 1.0));
  recordIndex = uint(gl_InstanceIndex);
  VectorRecord record = records[recordIndex];
  vec2 minimum = record.boundsMinimum.xy + record.boundsMinimum.zw;
  vec2 maximum = record.boundsMaximum.xy + record.boundsMaximum.zw;
  float coverPixels =
      record.meta.x == 11u ? max(abs(record.shape.z) + record.shape.x * 0.5,
                                 abs(record.shape.w) + record.shape.y * 0.5) +
                                 2.0
      : record.meta.x == 12u
          ? max(48.0, record.shape.x * 0.5 + 40.0) +
                length(record.shape.zw) + 2.0
      : record.meta.x == 10u
          ? max(record.appearance.x, record.appearance.y) * 0.5 +
                length(record.shape.zw) + 2.0
          : max(record.appearance.x, record.appearance.y) * 0.5 + 2.0;
  vec2 expansion = vec2(coverPixels * ubuf.cameraHigh.z);
  vec2 relative =
      mix(minimum - expansion, maximum + expansion, corners[gl_VertexIndex]);
  vec2 camera = ubuf.cameraHigh.xy + ubuf.cameraLowRotation.xy;
  vec2 delta = relative - camera;
  float cosine = ubuf.cameraLowRotation.z;
  float sine = ubuf.cameraLowRotation.w;
  vec2 item = ubuf.viewport.xy * 0.5;
  item += vec2(cosine * delta.x - sine * delta.y,
               -(sine * delta.x + cosine * delta.y)) /
          ubuf.cameraHigh.z;
  gl_Position = ubuf.matrix * vec4(item, 0.0, 1.0);
  queryRelativeMetres = relative;
}
