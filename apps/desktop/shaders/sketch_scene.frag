#version 440

const float PI = 3.14159265358979323846;
const float TAU = 6.28318530717958647692;
#ifdef KEARNE_NURBS_MAX_DEGREE
const int MAX_NURBS_DEGREE = KEARNE_NURBS_MAX_DEGREE;
#else
const int MAX_NURBS_DEGREE = 25;
#endif

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

layout(std430, binding = 2) readonly buffer NativeData { vec4 data[]; };

layout(location = 0) in vec2 queryRelativeMetres;
layout(location = 1) flat in uint recordIndex;
layout(location = 0) out vec4 fragmentColor;

float segmentDistance(vec2 query, vec2 first, vec2 second,
                      out float parameter) {
  vec2 delta = second - first;
  parameter = clamp(dot(query - first, delta) / dot(delta, delta), 0.0, 1.0);
  return length(query - (first + parameter * delta));
}

#ifndef KEARNE_NURBS_ONLY

float positiveAngle(float value) {
  float result = mod(value, TAU);
  return result < 0.0 ? result + TAU : result;
}

bool onSweep(float parameter, float start, float sweep) {
  return sweep >= 0.0 ? positiveAngle(parameter - start) <= sweep
                      : positiveAngle(start - parameter) <= -sweep;
}

#ifndef KEARNE_ANNOTATION_ONLY

vec2 rotateIntoLocal(vec2 point, float rotation) {
  float cosine = cos(rotation);
  float sine = sin(rotation);
  return vec2(cosine * point.x + sine * point.y,
              -sine * point.x + cosine * point.y);
}

vec2 ellipsePoint(float a, float b, float parameter) {
  return vec2(a * cos(parameter), b * sin(parameter));
}

vec2 ellipseDerivative(float a, float b, float parameter) {
  return vec2(-a * sin(parameter), b * cos(parameter));
}

float ellipseLambdaEquation(vec2 query, float ratio, float lambda) {
  float x = query.x / (lambda + 1.0);
  float y = ratio * query.y / (lambda + ratio * ratio);
  return x * x + y * y - 1.0;
}

float ellipseLambdaRoot(vec2 query, float ratio, float lower, float upper,
                        bool lowerIsPositive) {
  for (int iteration = 0; iteration < 24; ++iteration) {
    float midpoint = (lower + upper) * 0.5;
    bool positive = ellipseLambdaEquation(query, ratio, midpoint) > 0.0;
    if (positive == lowerIsPositive)
      lower = midpoint;
    else
      upper = midpoint;
  }
  return (lower + upper) * 0.5;
}

vec2 closestPointOnEllipse(vec2 query, float a, float b) {
  vec2 signValue = vec2(query.x < 0.0 ? -1.0 : 1.0, query.y < 0.0 ? -1.0 : 1.0);
  vec2 positive = abs(query) / a;
  float ratio = b / a;
  float ratioSquared = ratio * ratio;

  if (positive.x == 0.0)
    return vec2(0.0, signValue.y * b);
  if (positive.y == 0.0) {
    float threshold = 1.0 - ratioSquared;
    if (positive.x >= threshold)
      return vec2(signValue.x * a, 0.0);
    float x = positive.x / threshold;
    return signValue * vec2(a * x, b * sqrt(max(0.0, 1.0 - x * x)));
  }

  float atZero = ellipseLambdaEquation(positive, ratio, 0.0);
  float lower = atZero >= 0.0 ? 0.0 : -ratioSquared + ratio * positive.y;
  float upper = atZero >= 0.0 ? length(positive) : 0.0;
  float lambda = ellipseLambdaRoot(positive, ratio, lower, upper, true);
  return signValue *
         vec2(a * positive.x / (lambda + 1.0),
              a * ratioSquared * positive.y / (lambda + ratioSquared));
}

void considerEllipseArcPoint(vec2 query, float a, float b, float start,
                             float sweep, vec2 candidate, inout vec2 bestPoint,
                             inout float bestParameter,
                             inout float bestDistanceSquared) {
  float parameter = atan(candidate.y / b, candidate.x / a);
  if (!onSweep(parameter, start, sweep))
    return;
  float distanceSquared = dot(query - candidate, query - candidate);
  if (distanceSquared >= bestDistanceSquared)
    return;
  float offset = sweep >= 0.0 ? positiveAngle(parameter - start)
                              : positiveAngle(start - parameter);
  bestPoint = candidate;
  bestParameter = start + (sweep >= 0.0 ? offset : -offset);
  bestDistanceSquared = distanceSquared;
}

vec2 closestPointOnEllipseArc(vec2 query, float a, float b, float start,
                              float sweep, out float parameter) {
  vec2 finishPoint = ellipsePoint(a, b, start + sweep);
  vec2 bestPoint = ellipsePoint(a, b, start);
  parameter = start;
  float bestDistanceSquared = dot(query - bestPoint, query - bestPoint);
  float finishDistanceSquared = dot(query - finishPoint, query - finishPoint);
  if (finishDistanceSquared < bestDistanceSquared) {
    bestPoint = finishPoint;
    parameter = start + sweep;
    bestDistanceSquared = finishDistanceSquared;
  }

  vec2 normalized = query / a;
  float ratio = b / a;
  float ratioSquared = ratio * ratio;
  if (ratio == 1.0) {
    float magnitude = length(normalized);
    if (magnitude > 0.0)
      considerEllipseArcPoint(query, a, b, start, sweep,
                              a * normalized / magnitude, bestPoint, parameter,
                              bestDistanceSquared);
    return bestPoint;
  }

  if (normalized.y == 0.0) {
    float threshold = 1.0 - ratioSquared;
    if (abs(normalized.x) >= threshold) {
      vec2 candidate = vec2(normalized.x < 0.0 ? -a : a, 0.0);
      considerEllipseArcPoint(query, a, b, start, sweep, candidate, bestPoint,
                              parameter, bestDistanceSquared);
    } else {
      float x = normalized.x / threshold;
      float y = b * sqrt(max(0.0, 1.0 - x * x));
      considerEllipseArcPoint(query, a, b, start, sweep, vec2(a * x, y),
                              bestPoint, parameter, bestDistanceSquared);
      considerEllipseArcPoint(query, a, b, start, sweep, vec2(a * x, -y),
                              bestPoint, parameter, bestDistanceSquared);
    }
    return bestPoint;
  }
  if (normalized.x == 0.0) {
    float signY = normalized.y < 0.0 ? -1.0 : 1.0;
    considerEllipseArcPoint(query, a, b, start, sweep, vec2(0.0, signY * b),
                            bestPoint, parameter, bestDistanceSquared);
    if (ratio * abs(normalized.y) < 1.0 - ratioSquared)
      considerEllipseArcPoint(query, a, b, start, sweep, vec2(0.0, -signY * b),
                              bestPoint, parameter, bestDistanceSquared);
    return bestPoint;
  }

  float atZero = ellipseLambdaEquation(normalized, ratio, 0.0);
  float nearLower =
      atZero >= 0.0 ? 0.0 : -ratioSquared + ratio * abs(normalized.y);
  float nearUpper = atZero >= 0.0 ? length(normalized) : 0.0;
  float nearLambda =
      ellipseLambdaRoot(normalized, ratio, nearLower, nearUpper, true);
  vec2 nearPoint =
      a * vec2(normalized.x / (nearLambda + 1.0),
               ratioSquared * normalized.y / (nearLambda + ratioSquared));
  considerEllipseArcPoint(query, a, b, start, sweep, nearPoint, bestPoint,
                          parameter, bestDistanceSquared);

  float firstScale = pow(abs(normalized.x), 2.0 / 3.0);
  float secondScale = pow(abs(ratio * normalized.y), 2.0 / 3.0);
  float turningLambda =
      -(firstScale * ratioSquared + secondScale) / (firstScale + secondScale);
  if (ellipseLambdaEquation(normalized, ratio, turningLambda) <= 0.0) {
    vec2 lowerBounds = vec2(-1.0 + abs(normalized.x), turningLambda);
    vec2 upperBounds =
        vec2(turningLambda, -ratioSquared - abs(ratio * normalized.y));
    for (int rootIndex = 0; rootIndex < 2; ++rootIndex) {
      float lambda =
          ellipseLambdaRoot(normalized, ratio, lowerBounds[rootIndex],
                            upperBounds[rootIndex], rootIndex == 0);
      vec2 candidate =
          a * vec2(normalized.x / (lambda + 1.0),
                   ratioSquared * normalized.y / (lambda + ratioSquared));
      float sine = candidate.y / b;
      float cosine = candidate.x / a;
      float normalizedSpeedSquared =
          sine * sine + ratioSquared * cosine * cosine;
      if (lambda + normalizedSpeedSquared >= 0.0)
        considerEllipseArcPoint(query, a, b, start, sweep, candidate, bestPoint,
                                parameter, bestDistanceSquared);
    }
  }
  return bestPoint;
}

float ellipseArcLength(float a, float b, float first, float last) {
  float sum = 0.0;
  float step = (last - first) / 8.0;
  for (int index = 0; index < 8; ++index) {
    float parameter = first + (float(index) + 0.5) * step;
    sum += length(ellipseDerivative(a, b, parameter));
  }
  return abs(step) * sum;
}

vec2 conicPoint(uint kind, float a, float b, float parameter) {
  return kind == 7u ? vec2(a * cosh(parameter), b * sinh(parameter))
                    : vec2(parameter * parameter / (4.0 * a), parameter);
}

vec2 conicDerivative(uint kind, float a, float b, float parameter) {
  return kind == 7u ? vec2(a * sinh(parameter), b * cosh(parameter))
                    : vec2(parameter / (2.0 * a), 1.0);
}

vec2 conicSecondDerivative(uint kind, float a, float b, float parameter) {
  return kind == 7u ? vec2(a * cosh(parameter), b * sinh(parameter))
                    : vec2(1.0 / (2.0 * a), 0.0);
}

float conicParameter(uint kind, vec2 query, float a, float b, float first,
                     float last) {
  float parameter = kind == 7u ? asinh(query.y / b) : query.y;
  parameter = clamp(parameter, min(first, last), max(first, last));
  for (int iteration = 0; iteration < 12; ++iteration) {
    vec2 point = conicPoint(kind, a, b, parameter);
    vec2 derivative = conicDerivative(kind, a, b, parameter);
    vec2 second = conicSecondDerivative(kind, a, b, parameter);
    float denominator =
        dot(derivative, derivative) + dot(point - query, second);
    if (abs(denominator) < 1e-20)
      break;
    parameter = clamp(parameter - dot(point - query, derivative) / denominator,
                      min(first, last), max(first, last));
  }
  return parameter;
}

float conicArcLength(uint kind, float a, float b, float first, float last) {
  float sum = 0.0;
  float step = (last - first) / 8.0;
  for (int index = 0; index < 8; ++index) {
    float parameter = first + (float(index) + 0.5) * step;
    sum += length(conicDerivative(kind, a, b, parameter));
  }
  return abs(step) * sum;
}

#endif

#endif

#ifndef KEARNE_BASIC_ONLY

vec2 nurbsPoint(VectorRecord record, float parameter) {
  int degree = int(record.meta.z);
  int count = degree + 1;
  int offset = int(record.meta.y);
  int knotOffset = offset + count * 2;
  vec3 values[MAX_NURBS_DEGREE + 1];
  for (int index = 0; index <= MAX_NURBS_DEGREE; ++index) {
    if (index > degree)
      break;
    vec4 packed = data[offset + index];
    vec2 point = packed.xy + packed.zw;
    float weight = data[offset + count + index].x;
    values[index] = vec3(point * weight, weight);
  }
  for (int level = 1; level <= MAX_NURBS_DEGREE; ++level) {
    if (level > degree)
      break;
    for (int reverse = 0; reverse <= MAX_NURBS_DEGREE; ++reverse) {
      int index = degree - reverse;
      if (index < level)
        break;
      float lower = data[knotOffset + index].x;
      float upper = data[knotOffset + index + 1 + degree - level].x;
      float alpha =
          upper == lower ? 0.0 : (parameter - lower) / (upper - lower);
      values[index] = mix(values[index - 1], values[index], alpha);
    }
  }
  return values[degree].xy / values[degree].z;
}

struct NurbsSample {
  vec2 point;
  vec2 first;
  vec2 second;
};

NurbsSample nurbsSample(VectorRecord record, float parameter) {
  int degree = int(record.meta.z);
  int count = degree + 1;
  int offset = int(record.meta.y);
  int knotOffset = offset + count * 2;
  vec3 values[MAX_NURBS_DEGREE + 1];
  vec3 firstDerivatives[MAX_NURBS_DEGREE + 1];
  vec3 secondDerivatives[MAX_NURBS_DEGREE + 1];
  for (int index = 0; index <= MAX_NURBS_DEGREE; ++index) {
    if (index > degree)
      break;
    vec4 packed = data[offset + index];
    vec2 point = packed.xy + packed.zw;
    float weight = data[offset + count + index].x;
    values[index] = vec3(point * weight, weight);
    firstDerivatives[index] = vec3(0.0);
    secondDerivatives[index] = vec3(0.0);
  }
  for (int level = 1; level <= MAX_NURBS_DEGREE; ++level) {
    if (level > degree)
      break;
    for (int reverse = 0; reverse <= MAX_NURBS_DEGREE; ++reverse) {
      int index = degree - reverse;
      if (index < level)
        break;
      float lower = data[knotOffset + index].x;
      float upper = data[knotOffset + index + 1 + degree - level].x;
      float inverseWidth = upper == lower ? 0.0 : 1.0 / (upper - lower);
      float alpha = (parameter - lower) * inverseWidth;
      vec3 previousValue = values[index - 1];
      vec3 previousFirst = firstDerivatives[index - 1];
      vec3 previousSecond = secondDerivatives[index - 1];
      vec3 currentValue = values[index];
      vec3 currentFirst = firstDerivatives[index];
      vec3 currentSecond = secondDerivatives[index];
      values[index] = mix(previousValue, currentValue, alpha);
      firstDerivatives[index] = mix(previousFirst, currentFirst, alpha) +
                                (currentValue - previousValue) * inverseWidth;
      secondDerivatives[index] =
          mix(previousSecond, currentSecond, alpha) +
          2.0 * (currentFirst - previousFirst) * inverseWidth;
    }
  }
  vec3 value = values[degree];
  vec3 homogeneousFirst = firstDerivatives[degree];
  vec3 homogeneousSecond = secondDerivatives[degree];
  vec2 point = value.xy / value.z;
  vec2 first = (homogeneousFirst.xy - point * homogeneousFirst.z) / value.z;
  vec2 second = (homogeneousSecond.xy - 2.0 * first * homogeneousFirst.z -
                 point * homogeneousSecond.z) /
                value.z;
  return NurbsSample(point, first, second);
}

float nurbsParameter(VectorRecord record, vec2 query) {
  float first = record.domain.y;
  float last = record.domain.z;
  float best = first;
  float bestDistance = 3.402823466e+38;
  int seedCount = record.meta.z == 2u ? 4 : 12;
  for (int index = 0; index <= 12; ++index) {
    if (index > seedCount)
      break;
    float parameter = mix(first, last, float(index) / float(seedCount));
    vec2 delta = nurbsPoint(record, parameter) - query;
    float candidate = dot(delta, delta);
    if (candidate < bestDistance) {
      bestDistance = candidate;
      best = parameter;
    }
  }
  for (int iteration = 0; iteration < 10; ++iteration) {
    if (record.meta.z == 2u && iteration == 6)
      break;
    NurbsSample evaluation = nurbsSample(record, best);
    float denominator = dot(evaluation.first, evaluation.first) +
                        dot(evaluation.point - query, evaluation.second);
    if (abs(denominator) < 1e-20)
      break;
    best = clamp(best - dot(evaluation.point - query, evaluation.first) /
                            denominator,
                 first, last);
  }
  return best;
}

float nurbsArcLength(VectorRecord record, float first, float last) {
  float sum = 0.0;
  float step = (last - first) / 8.0;
  for (int index = 0; index < 8; ++index) {
    float parameter = first + (float(index) + 0.5) * step;
    sum += length(nurbsSample(record, parameter).first);
  }
  return abs(step) * sum;
}

#endif

#ifdef KEARNE_ANNOTATION_ONLY

float markerLine(vec2 queryPixels, vec2 first, vec2 second, float widthPixels) {
  float parameter = 0.0;
  return segmentDistance(queryPixels, first, second, parameter) - widthPixels;
}

float markerRing(vec2 queryPixels, vec2 center, float ringRadius,
                 float widthPixels) {
  return abs(length(queryPixels - center) - ringRadius) - widthPixels;
}

float markerDot(vec2 queryPixels, vec2 center, float dotRadius) {
  return length(queryPixels - center) - dotRadius;
}

float markerHorizontalArrow(vec2 queryPixels, float radius, float widthPixels) {
  float extent = radius * 0.82;
  float head = radius * 0.34;
  float result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                            widthPixels);
  result = min(result, markerLine(queryPixels, vec2(-extent, 0.0),
                                  vec2(-extent + head, -head), widthPixels));
  result = min(result, markerLine(queryPixels, vec2(-extent, 0.0),
                                  vec2(-extent + head, head), widthPixels));
  result = min(result, markerLine(queryPixels, vec2(extent, 0.0),
                                  vec2(extent - head, -head), widthPixels));
  return min(result, markerLine(queryPixels, vec2(extent, 0.0),
                                vec2(extent - head, head), widthPixels));
}

float markerVerticalArrow(vec2 queryPixels, float radius, float widthPixels) {
  return markerHorizontalArrow(queryPixels.yx, radius, widthPixels);
}

float markerDistance(uint glyph, vec2 queryPixels, float radius) {
  float widthPixels = max(0.8, radius * 0.12);
  float extent = radius * 0.78;
  float halfExtent = radius * 0.42;
  float result = 1000.0;

  if (glyph == 1u) { // Coincident
    result = markerRing(queryPixels, vec2(0.0), halfExtent, widthPixels);
    return min(result, markerDot(queryPixels, vec2(0.0), widthPixels));
  }
  if (glyph == 2u) { // Horizontal
    result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                        widthPixels);
    result =
        min(result, markerLine(queryPixels, vec2(-extent * 0.58, -halfExtent),
                               vec2(-extent * 0.58, halfExtent), widthPixels));
    return min(result,
               markerLine(queryPixels, vec2(extent * 0.58, -halfExtent),
                          vec2(extent * 0.58, halfExtent), widthPixels));
  }
  if (glyph == 3u) { // Vertical
    result = markerLine(queryPixels, vec2(0.0, -extent), vec2(0.0, extent),
                        widthPixels);
    result =
        min(result, markerLine(queryPixels, vec2(-halfExtent, -extent * 0.58),
                               vec2(halfExtent, -extent * 0.58), widthPixels));
    return min(result,
               markerLine(queryPixels, vec2(-halfExtent, extent * 0.58),
                          vec2(halfExtent, extent * 0.58), widthPixels));
  }
  if (glyph == 4u) { // Parallel
    result = markerLine(queryPixels, vec2(-extent, -halfExtent),
                        vec2(halfExtent, extent), widthPixels);
    return min(result, markerLine(queryPixels, vec2(-halfExtent, -extent),
                                  vec2(extent, halfExtent), widthPixels));
  }
  if (glyph == 5u) { // Perpendicular
    result = markerLine(queryPixels, vec2(-extent, -extent),
                        vec2(-extent, extent), widthPixels);
    result = min(result, markerLine(queryPixels, vec2(-extent, extent),
                                    vec2(extent, extent), widthPixels));
    result =
        min(result, markerLine(queryPixels, vec2(-extent, halfExtent),
                               vec2(-halfExtent, halfExtent), widthPixels));
    return min(result, markerLine(queryPixels, vec2(-halfExtent, halfExtent),
                                  vec2(-halfExtent, extent), widthPixels));
  }
  if (glyph == 6u) { // Tangent
    result = markerRing(queryPixels, vec2(0.0, -radius * 0.18), halfExtent,
                        widthPixels);
    return min(result, markerLine(queryPixels, vec2(-extent, radius * 0.28),
                                  vec2(extent, radius * 0.28), widthPixels));
  }
  if (glyph == 7u) { // Equal
    result = markerLine(queryPixels, vec2(-extent, -radius * 0.28),
                        vec2(extent, -radius * 0.28), widthPixels);
    return min(result, markerLine(queryPixels, vec2(-extent, radius * 0.28),
                                  vec2(extent, radius * 0.28), widthPixels));
  }
  if (glyph == 8u) { // Concentric
    result = markerRing(queryPixels, vec2(0.0), radius * 0.7, widthPixels);
    return min(result,
               markerRing(queryPixels, vec2(0.0), radius * 0.34, widthPixels));
  }
  if (glyph == 9u) { // Midpoint
    result = markerLine(queryPixels, vec2(0.0, -extent), vec2(-extent, extent),
                        widthPixels);
    result = min(result, markerLine(queryPixels, vec2(-extent, extent),
                                    vec2(extent, extent), widthPixels));
    return min(result, markerLine(queryPixels, vec2(extent, extent),
                                  vec2(0.0, -extent), widthPixels));
  }
  if (glyph == 10u) { // Fixed / blocked
    result = markerLine(queryPixels, vec2(-halfExtent, -radius * 0.05),
                        vec2(halfExtent, -radius * 0.05), widthPixels);
    result =
        min(result, markerLine(queryPixels, vec2(-halfExtent, -radius * 0.05),
                               vec2(-halfExtent, extent), widthPixels));
    result =
        min(result, markerLine(queryPixels, vec2(halfExtent, -radius * 0.05),
                               vec2(halfExtent, extent), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(-halfExtent, extent),
                                    vec2(halfExtent, extent), widthPixels));
    float shackle = markerRing(queryPixels, vec2(0.0, -radius * 0.08),
                               radius * 0.36, widthPixels);
    shackle = queryPixels.y > -radius * 0.08 ? 1000.0 : shackle;
    return min(result, shackle);
  }
  if (glyph == 11u) { // Collinear
    result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                        widthPixels);
    result = min(result, markerDot(queryPixels, vec2(-halfExtent, 0.0),
                                   widthPixels * 1.35));
    result = min(result, markerDot(queryPixels, vec2(0.0), widthPixels * 1.35));
    return min(result, markerDot(queryPixels, vec2(halfExtent, 0.0),
                                 widthPixels * 1.35));
  }
  if (glyph == 12u) { // Point on object
    result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                        widthPixels);
    return min(result,
               markerRing(queryPixels, vec2(0.0), radius * 0.31, widthPixels));
  }
  if (glyph == 13u) { // Symmetric about line
    result = markerLine(queryPixels, vec2(0.0, -extent), vec2(0.0, extent),
                        widthPixels * 0.72);
    result = min(result, markerLine(queryPixels, vec2(-extent, -halfExtent),
                                    vec2(-halfExtent, 0.0), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(-extent, halfExtent),
                                    vec2(-halfExtent, 0.0), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(extent, -halfExtent),
                                    vec2(halfExtent, 0.0), widthPixels));
    return min(result, markerLine(queryPixels, vec2(extent, halfExtent),
                                  vec2(halfExtent, 0.0), widthPixels));
  }
  if (glyph == 14u) { // Symmetric about point
    result = markerDot(queryPixels, vec2(0.0), widthPixels * 1.4);
    result = min(result, markerRing(queryPixels, vec2(-extent * 0.72, 0.0),
                                    radius * 0.22, widthPixels));
    return min(result, markerRing(queryPixels, vec2(extent * 0.72, 0.0),
                                  radius * 0.22, widthPixels));
  }
  if (glyph == 15u) { // Rigid group
    result = markerLine(queryPixels, vec2(-extent, -extent),
                        vec2(-extent, extent), widthPixels);
    result = min(result, markerLine(queryPixels, vec2(-extent, -extent),
                                    vec2(-halfExtent, -extent), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(-extent, extent),
                                    vec2(-halfExtent, extent), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(extent, -extent),
                                    vec2(extent, extent), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(halfExtent, -extent),
                                    vec2(extent, -extent), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(halfExtent, extent),
                                    vec2(extent, extent), widthPixels));
    result = min(result, markerDot(queryPixels, vec2(-halfExtent, 0.0),
                                   widthPixels * 1.35));
    result = min(result, markerDot(queryPixels, vec2(0.0), widthPixels * 1.35));
    return min(result, markerDot(queryPixels, vec2(halfExtent, 0.0),
                                 widthPixels * 1.35));
  }
  if (glyph == 16u) { // Refraction
    result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                        widthPixels * 0.72);
    result = min(result, markerLine(queryPixels, vec2(-extent * 0.72, -extent),
                                    vec2(0.0, 0.0), widthPixels));
    result = min(result, markerLine(queryPixels, vec2(0.0, 0.0),
                                    vec2(extent * 0.42, extent), widthPixels));
    return min(result, markerDot(queryPixels, vec2(0.0), widthPixels * 1.3));
  }
  if (glyph == 96u) // Aligned distance
    return markerHorizontalArrow(
        vec2((queryPixels.x + queryPixels.y) * 0.70710678,
             (queryPixels.y - queryPixels.x) * 0.70710678),
        radius, widthPixels);
  if (glyph == 97u)
    return markerHorizontalArrow(queryPixels, radius, widthPixels);
  if (glyph == 98u)
    return markerVerticalArrow(queryPixels, radius, widthPixels);
  if (glyph == 99u) { // Radius
    result =
        markerRing(queryPixels, vec2(0.0), radius * 0.72, widthPixels * 0.72);
    result = min(result,
                 markerLine(queryPixels, vec2(0.0),
                            vec2(extent * 0.72, -extent * 0.72), widthPixels));
    return min(result, markerDot(queryPixels, vec2(0.0), widthPixels * 1.25));
  }
  if (glyph == 100u) { // Diameter
    result =
        markerRing(queryPixels, vec2(0.0), radius * 0.72, widthPixels * 0.72);
    return min(result,
               markerLine(queryPixels, vec2(-extent * 0.72, extent * 0.72),
                          vec2(extent * 0.72, -extent * 0.72), widthPixels));
  }
  if (glyph == 101u) { // Angle
    result = markerLine(queryPixels, vec2(-extent, extent), vec2(0.0, 0.0),
                        widthPixels);
    result = min(result, markerLine(queryPixels, vec2(0.0, 0.0),
                                    vec2(extent, extent), widthPixels));
    float angleArc =
        markerRing(queryPixels, vec2(0.0), radius * 0.48, widthPixels);
    if (queryPixels.y < 0.0 || abs(queryPixels.x) > queryPixels.y)
      angleArc = 1000.0;
    return min(result, angleArc);
  }
  if (glyph >= 128u) {
    result = markerLine(queryPixels, vec2(-extent, 0.0), vec2(extent, 0.0),
                        widthPixels);
    result = min(result, markerLine(queryPixels, vec2(0.0, -extent),
                                    vec2(0.0, extent), widthPixels));
    return min(result,
               markerRing(queryPixels, vec2(0.0), halfExtent, widthPixels));
  }
  if (glyph >= 64u)
    return markerHorizontalArrow(queryPixels, radius, widthPixels);
  if (glyph >= 32u)
    return min(markerLine(queryPixels, vec2(-extent, -extent),
                          vec2(extent, extent), widthPixels),
               markerRing(queryPixels, vec2(0.0), radius * 0.68, widthPixels));
  return markerRing(queryPixels, vec2(0.0), halfExtent, widthPixels);
}

float textSegment(vec2 query, vec2 first, vec2 second) {
  float parameter = 0.0;
  return segmentDistance(query, first, second, parameter);
}

float textGlyphDistance(uint code, vec2 query) {
  const vec4 segments[7] =
      vec4[7](vec4(-2.0, -3.0, 2.0, -3.0), vec4(2.0, -3.0, 2.0, 0.0),
              vec4(2.0, 0.0, 2.0, 3.0), vec4(-2.0, 3.0, 2.0, 3.0),
              vec4(-2.0, 0.0, -2.0, 3.0), vec4(-2.0, -3.0, -2.0, 0.0),
              vec4(-2.0, 0.0, 2.0, 0.0));
  const uint masks[10] =
      uint[10](63u, 6u, 91u, 79u, 102u, 109u, 125u, 7u, 127u, 111u);
  float distanceValue = 1000.0;
  if (code == 49u) {
    distanceValue =
        min(textSegment(query, vec2(0.0, -3.0), vec2(0.0, 3.0)),
            min(textSegment(query, vec2(-1.0, -2.0), vec2(0.0, -3.0)),
                textSegment(query, vec2(-1.0, 3.0), vec2(1.0, 3.0))));
  } else if (code == 51u) {
    const vec4 strokes[8] =
        vec4[8](vec4(-1.5, -3.0, 0.7, -3.0), vec4(0.7, -3.0, 1.7, -2.2),
                vec4(1.7, -2.2, 1.7, -1.2), vec4(1.7, -1.2, 0.5, 0.0),
                vec4(0.5, 0.0, 1.7, 1.2), vec4(1.7, 1.2, 1.7, 2.2),
                vec4(1.7, 2.2, 0.7, 3.0), vec4(0.7, 3.0, -1.5, 3.0));
    for (int stroke = 0; stroke < 8; ++stroke) {
      vec4 line = strokes[stroke];
      distanceValue = min(distanceValue, textSegment(query, line.xy, line.zw));
    }
  } else if (code >= 48u && code <= 57u) {
    uint mask = masks[code - 48u];
    for (int segment = 0; segment < 7; ++segment) {
      if ((mask & (1u << uint(segment))) == 0u)
        continue;
      vec4 line = segments[segment];
      distanceValue = min(distanceValue, textSegment(query, line.xy, line.zw));
    }
  } else if (code == 40u || code == 41u) {
    float side = code == 40u ? -1.0 : 1.0;
    distanceValue = min(
        textSegment(query, vec2(side * 0.5, -3.0), vec2(side * 1.5, -1.8)),
        min(textSegment(query, vec2(side * 1.5, -1.8), vec2(side * 1.5, 1.8)),
            textSegment(query, vec2(side * 1.5, 1.8), vec2(side * 0.5, 3.0))));
  } else if (code == 91u || code == 93u) {
    float side = code == 91u ? -1.5 : 1.5;
    distanceValue =
        min(textSegment(query, vec2(side, -3.0), vec2(side, 3.0)),
            min(textSegment(query, vec2(side, -3.0), vec2(0.0, -3.0)),
                textSegment(query, vec2(side, 3.0), vec2(0.0, 3.0))));
  } else if (code == 45u) {
    distanceValue = textSegment(query, vec2(-1.8, 0.0), vec2(1.8, 0.0));
  } else if (code == 46u) {
    distanceValue = length(query - vec2(0.0, 3.0)) - 0.45;
  } else if (code == 176u) {
    distanceValue = abs(length(query - vec2(0.8, -1.8)) - 1.0) - 0.38;
  }
  return distanceValue - 0.42;
}

float packedTextDistance(uint offset, uint count, float heightPixels,
                         vec2 queryPixels) {
  count = min(count, 32u);
  float unit = heightPixels / 8.0;
  float advance = 5.0 * unit;
  float firstCenter = -0.5 * float(count - 1u) * advance;
  float distanceValue = 1000.0;
  for (uint character = 0u; character < 32u; ++character) {
    if (character >= count)
      break;
    vec4 packed = data[int(offset + character / 4u)];
    uint code = uint(packed[int(character % 4u)] + 0.5);
    vec2 local =
        (queryPixels - vec2(firstCenter + float(character) * advance, 0.0)) /
        unit;
    distanceValue = min(distanceValue, textGlyphDistance(code, local) * unit);
  }
  return distanceValue;
}

float textDistance(VectorRecord record, vec2 queryPixels) {
  return packedTextDistance(record.meta.y, record.meta.z, record.shape.y,
                            queryPixels);
}

vec2 canonicalDeltaToScreen(vec2 delta) {
  float cosine = ubuf.cameraLowRotation.z;
  float sine = ubuf.cameraLowRotation.w;
  return vec2(cosine * delta.x - sine * delta.y,
              -(sine * delta.x + cosine * delta.y)) /
         ubuf.cameraHigh.z;
}

float dimensionArrow(vec2 query, vec2 tip, vec2 inward, float widthPixels) {
  vec2 normal = vec2(-inward.y, inward.x);
  return min(
      markerLine(query, tip, tip + inward * 7.0 + normal * 3.2, widthPixels),
      markerLine(query, tip, tip + inward * 7.0 - normal * 3.2, widthPixels));
}

float dimensionText(VectorRecord record, vec2 query, vec2 center) {
  return packedTextDistance(record.meta.y + 1u, uint(record.domain.x + 0.5),
                            record.shape.y, query - center);
}

float linearDimensionDistance(VectorRecord record, vec2 query, vec2 second) {
  float widthPixels = max(0.9, record.appearance.x);
  vec2 firstDimension = vec2(0.0);
  vec2 secondDimension = second;
  vec2 direction;
  if (record.meta.z == 97u) {
    firstDimension.y = -24.0;
    secondDimension = vec2(second.x, -24.0);
    direction = vec2(second.x < 0.0 ? -1.0 : 1.0, 0.0);
  } else if (record.meta.z == 98u) {
    firstDimension.x = 24.0;
    secondDimension = vec2(24.0, second.y);
    direction = vec2(0.0, second.y < 0.0 ? -1.0 : 1.0);
  } else {
    direction = normalize(second);
    vec2 normal = vec2(-direction.y, direction.x);
    if (normal.y > 0.0)
      normal = -normal;
    firstDimension = normal * 24.0;
    secondDimension = second + normal * 24.0;
  }
  firstDimension += record.shape.zw;
  secondDimension += record.shape.zw;
  vec2 center = (firstDimension + secondDimension) * 0.5;
  float halfText = record.shape.x * 0.5 + 3.0;
  float halfLength = length(secondDimension - firstDimension) * 0.5;
  float gap = min(halfText, max(0.0, halfLength - 8.0));
  float result =
      markerLine(query, vec2(0.0), firstDimension, widthPixels * 0.72);
  result = min(result,
               markerLine(query, second, secondDimension, widthPixels * 0.72));
  result = min(result, markerLine(query, firstDimension,
                                  center - direction * gap, widthPixels));
  result = min(result, markerLine(query, center + direction * gap,
                                  secondDimension, widthPixels));
  result = min(result,
               dimensionArrow(query, firstDimension, direction, widthPixels));
  result = min(result,
               dimensionArrow(query, secondDimension, -direction, widthPixels));
  return min(result, dimensionText(record, query, center));
}

float radialDimensionDistance(VectorRecord record, vec2 query, vec2 rim) {
  float widthPixels = max(0.9, record.appearance.x);
  float rimLength = length(rim);
  vec2 direction = rimLength > 0.0 ? rim / rimLength : vec2(1.0, 0.0);
  bool diameter = record.meta.z == 100u;
  vec2 first = diameter ? -rim : vec2(0.0);
  vec2 finish = rim + direction * 24.0 + record.shape.zw;
  vec2 textCenter = finish + direction * (record.shape.x * 0.5 + 3.0);
  float result = markerLine(query, first, finish, widthPixels);
  result = min(result, dimensionArrow(query, rim, -direction, widthPixels));
  if (diameter)
    result = min(result, dimensionArrow(query, -rim, direction, widthPixels));
  return min(result, dimensionText(record, query, textCenter));
}

float angularDimensionDistance(VectorRecord record, vec2 query, vec2 firstRay,
                               vec2 secondRay) {
  float widthPixels = max(0.9, record.appearance.x);
  vec2 firstDirection = normalize(firstRay);
  vec2 secondDirection = normalize(secondRay);
  float firstAngle = atan(firstDirection.y, firstDirection.x);
  float secondAngle = atan(secondDirection.y, secondDirection.x);
  float sweep = positiveAngle(secondAngle - firstAngle);
  if (sweep > PI) {
    float replacement = firstAngle;
    firstAngle = secondAngle;
    secondAngle = replacement;
    sweep = TAU - sweep;
    vec2 replacementDirection = firstDirection;
    firstDirection = secondDirection;
    secondDirection = replacementDirection;
  }
  float radius = 30.0 + length(record.shape.zw);
  float queryAngle = atan(query.y, query.x);
  float arcDistance = 1000.0;
  if (onSweep(queryAngle, firstAngle, sweep))
    arcDistance = abs(length(query) - radius) - widthPixels * 0.5;
  vec2 firstArc = firstDirection * radius;
  vec2 secondArc = secondDirection * radius;
  float result =
      min(markerLine(query, vec2(0.0), firstDirection * (radius + 5.0),
                     widthPixels * 0.72),
          markerLine(query, vec2(0.0), secondDirection * (radius + 5.0),
                     widthPixels * 0.72));
  result = min(result, arcDistance);
  vec2 firstTangent = vec2(-firstDirection.y, firstDirection.x);
  vec2 secondTangent = vec2(-secondDirection.y, secondDirection.x);
  result =
      min(result, dimensionArrow(query, firstArc, firstTangent, widthPixels));
  result = min(result,
               dimensionArrow(query, secondArc, -secondTangent, widthPixels));
  vec2 bisector =
      vec2(cos(firstAngle + sweep * 0.5), sin(firstAngle + sweep * 0.5));
  vec2 textCenter = bisector * (radius + 11.0 + record.shape.y * 0.5);
  return min(result, dimensionText(record, query, textCenter));
}

float dimensionDistance(VectorRecord record, vec2 queryCanonical) {
  vec2 first = record.first.xy + record.first.zw;
  vec2 second = record.second.xy + record.second.zw;
  vec4 packedThird = data[int(record.meta.y)];
  vec2 third = packedThird.xy + packedThird.zw;
  vec2 query = canonicalDeltaToScreen(queryCanonical - first);
  vec2 secondScreen = canonicalDeltaToScreen(second - first);
  if (record.meta.z <= 98u)
    return linearDimensionDistance(record, query, secondScreen);
  if (record.meta.z <= 100u)
    return radialDimensionDistance(record, query, secondScreen);
  return angularDimensionDistance(record, query, secondScreen,
                                  canonicalDeltaToScreen(third - first));
}

#endif

void main() {
  VectorRecord record = records[recordIndex];
  uint kind = record.meta.x;
  vec2 query = queryRelativeMetres;
  vec2 first = record.first.xy + record.first.zw;
  vec2 second = record.second.xy + record.second.zw;
  float distanceMetres = 0.0;
  float pathMetres = record.domain.w;
  float radiusPixels = record.appearance.y * 0.5;
  vec4 resolvedColor = ubuf.color;

#ifdef KEARNE_NURBS_ONLY
  if (kind != 9u)
    discard;
#ifdef KEARNE_NURBS_MAX_DEGREE
  if (record.meta.z > uint(KEARNE_NURBS_MAX_DEGREE))
    discard;
#endif
  if (record.meta.z == 1u) {
    int offset = int(record.meta.y);
    vec4 packedStart = data[offset];
    vec4 packedFinish = data[offset + 1];
    vec2 start = packedStart.xy + packedStart.zw;
    vec2 finish = packedFinish.xy + packedFinish.zw;
    float parameter = 0.0;
    distanceMetres = segmentDistance(query, start, finish, parameter);
    if (record.appearance.w > 0.0)
      pathMetres += parameter * length(finish - start);
  } else {
    float parameter = nurbsParameter(record, query);
    distanceMetres = length(query - nurbsPoint(record, parameter));
    if (record.appearance.w > 0.0)
      pathMetres += nurbsArcLength(record, record.domain.y, parameter);
  }
#elif defined(KEARNE_ANNOTATION_ONLY)
  if (kind == 10u) {
    vec2 queryPixels =
        canonicalDeltaToScreen(query - first) - record.shape.zw;
    float signedPixels =
        markerDistance(record.meta.z, queryPixels, radiusPixels);
    float antialias = max(fwidth(signedPixels), 0.35);
    float opacity = 1.0 - smoothstep(-antialias, antialias, signedPixels);
    if (opacity <= 0.0)
      discard;
    fragmentColor = resolvedColor * (ubuf.cameraHigh.w * opacity);
    return;
  } else if (kind == 11u) {
    vec2 delta = (query - first) / ubuf.cameraHigh.z;
    float cosine = ubuf.cameraLowRotation.z;
    float sine = ubuf.cameraLowRotation.w;
    vec2 queryPixels = vec2(cosine * delta.x - sine * delta.y,
                            -(sine * delta.x + cosine * delta.y));
    queryPixels -= record.shape.zw;
    float signedPixels = textDistance(record, queryPixels);
    float antialias = max(fwidth(signedPixels), 0.35);
    float opacity = 1.0 - smoothstep(-antialias, antialias, signedPixels);
    if (opacity <= 0.0)
      discard;
    fragmentColor = resolvedColor * (ubuf.cameraHigh.w * opacity);
    return;
  } else if (kind == 12u) {
    float signedPixels = dimensionDistance(record, query);
    float antialias = max(fwidth(signedPixels), 0.35);
    float opacity = 1.0 - smoothstep(-antialias, antialias, signedPixels);
    if (opacity <= 0.0)
      discard;
    fragmentColor = resolvedColor * (ubuf.cameraHigh.w * opacity);
    return;
  } else {
    discard;
  }
#else
  if (kind == 1u) {
    distanceMetres = length(query - first) - radiusPixels * ubuf.cameraHigh.z;
  } else if (kind == 2u) {
    float parameter = 0.0;
    distanceMetres = segmentDistance(query, first, second, parameter);
    if (record.appearance.w > 0.0)
      pathMetres += parameter * length(second - first);
  } else if (kind == 3u) {
    vec2 delta = query - first;
    distanceMetres = abs(length(delta) - record.shape.x);
    if (record.appearance.w > 0.0) {
      float parameter = atan(delta.y, delta.x);
      pathMetres += positiveAngle(parameter) * record.shape.x;
    }
  } else if (kind == 4u) {
    vec2 delta = query - first;
    float parameter = atan(delta.y, delta.x);
    float start = record.domain.y;
    float sweep = record.domain.z - start;
    if (!onSweep(parameter, start, sweep)) {
      vec2 startPoint = first + record.shape.x * vec2(cos(start), sin(start));
      vec2 endPoint =
          first + record.shape.x * vec2(cos(start + sweep), sin(start + sweep));
      bool useStart = length(query - startPoint) <= length(query - endPoint);
      distanceMetres =
          useStart ? length(query - startPoint) : length(query - endPoint);
      if (record.appearance.w > 0.0)
        pathMetres += useStart ? 0.0 : abs(sweep) * record.shape.x;
    } else {
      distanceMetres = abs(length(delta) - record.shape.x);
      if (record.appearance.w > 0.0)
        pathMetres += positiveAngle(sweep >= 0.0 ? parameter - start
                                                 : start - parameter) *
                      record.shape.x;
    }
  } else if (kind == 5u || kind == 6u) {
    vec2 local = rotateIntoLocal(query - first, record.domain.x);
    float start = record.domain.y;
    float sweep = record.domain.z - start;
    if (kind == 5u) {
      vec2 closest =
          closestPointOnEllipse(local, record.shape.x, record.shape.y);
      distanceMetres = length(local - closest);
      if (record.appearance.w > 0.0) {
        float parameter =
            atan(closest.y / record.shape.y, closest.x / record.shape.x);
        pathMetres += ellipseArcLength(record.shape.x, record.shape.y, 0.0,
                                       positiveAngle(parameter));
      }
    } else {
      float parameter = 0.0;
      vec2 closest = closestPointOnEllipseArc(
          local, record.shape.x, record.shape.y, start, sweep, parameter);
      distanceMetres = length(local - closest);
      if (record.appearance.w > 0.0)
        pathMetres +=
            ellipseArcLength(record.shape.x, record.shape.y, start, parameter);
    }
  } else if (kind == 7u || kind == 8u) {
    vec2 local = rotateIntoLocal(query - first, record.domain.x);
    float parameter =
        conicParameter(kind, local, record.shape.x, record.shape.y,
                       record.domain.y, record.domain.z);
    distanceMetres = length(
        local - conicPoint(kind, record.shape.x, record.shape.y, parameter));
    if (record.appearance.w > 0.0)
      pathMetres += conicArcLength(kind, record.shape.x, record.shape.y,
                                   record.domain.y, parameter);
  } else {
    discard;
  }
#endif

  float signedPixels = kind == 1u ? distanceMetres / ubuf.cameraHigh.z
                                  : distanceMetres / ubuf.cameraHigh.z -
                                        record.appearance.x * 0.5;
  float antialias = max(fwidth(signedPixels), 0.35);
  float opacity = 1.0 - smoothstep(-antialias, antialias, signedPixels);
  float period = record.appearance.w;
  if (period > 0.0 && kind != 1u) {
    float phase = mod(pathMetres / ubuf.cameraHigh.z, period);
    float patternEdge = max(fwidth(phase), 0.35);
    opacity *= 1.0 - smoothstep(record.appearance.z - patternEdge,
                                record.appearance.z + patternEdge, phase);
  }
  if (opacity <= 0.0)
    discard;
  fragmentColor = resolvedColor * (ubuf.cameraHigh.w * opacity);
}
