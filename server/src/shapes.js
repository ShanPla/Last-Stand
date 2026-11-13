// Shape checks for the parts of a message the server stores or passes on without reading:
// the host's run settings, a WebRTC signal, and a ping number.
//
// Why this matters: JSON.parse happily builds a value nested thousands of levels deep from a
// few KB of brackets, but JSON.stringify then runs out of stack and throws. Anything the server
// echoes back must therefore be checked first, so a hostile client cannot make a send throw.
// The checks below never recurse, so they cannot run out of stack themselves.

/** Most characters `JSON.stringify(settings)` may have. The browser sends about 30. */
export const MAX_SETTINGS_JSON = 200;
/** Most keys in the settings object. The browser sends 3 (map, difficulty, mode). */
export const MAX_SETTINGS_KEYS = 8;
/** Deepest nesting in a signal's data: the data object is level 1. An ICE candidate needs 2. */
export const MAX_SIGNAL_DEPTH = 4;
/** Most characters `JSON.stringify(data)` of a signal may have. A real offer is about 1 KB. */
export const MAX_SIGNAL_JSON = 8 * 1024;

/** True for `{...}`: not null, not an array. */
export function isPlainObject(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** True for a value that has no children: a string, a finite number, a boolean or null. */
function isLeaf(value) {
  return (
    value === null ||
    typeof value === 'string' ||
    typeof value === 'boolean' ||
    (typeof value === 'number' && Number.isFinite(value))
  );
}

/** The length of the JSON text for a value, or Infinity if it cannot be written as JSON. */
export function jsonLength(value) {
  try {
    const text = JSON.stringify(value);
    return typeof text === 'string' ? text.length : Infinity;
  } catch {
    return Infinity;
  }
}

/** A ping's `n` is echoed in the pong, so it must be a plain finite number. */
export function isPingNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

/**
 * The run settings: a flat object of at most MAX_SETTINGS_KEYS keys, each value a finite number,
 * a string or a boolean, and at most MAX_SETTINGS_JSON characters as JSON.
 */
export function isFlatSettings(settings) {
  if (!isPlainObject(settings)) return false;
  const keys = Object.keys(settings);
  if (keys.length > MAX_SETTINGS_KEYS) return false;
  for (const key of keys) {
    const value = settings[key];
    if (value === null || !isLeaf(value)) return false;
  }
  return jsonLength(settings) <= MAX_SETTINGS_JSON;
}

/**
 * A signal's data: a plain object, nested at most MAX_SIGNAL_DEPTH levels (objects and arrays),
 * with only strings, finite numbers, booleans and null at the ends, and at most MAX_SIGNAL_JSON
 * characters as JSON. Real signals are `{kind, sdp}` and `{kind, candidate: {...}}`.
 */
export function isSignalData(data) {
  if (!isPlainObject(data)) return false;
  // Walk with our own stack instead of recursion. The depth check stops the walk early, so a
  // deeply nested value costs at most MAX_SIGNAL_DEPTH levels of work before it is refused.
  const stack = [[data, 1]];
  while (stack.length > 0) {
    const [node, depth] = stack.pop();
    const children = Array.isArray(node) ? node : Object.values(node);
    for (const child of children) {
      if (isLeaf(child)) continue;
      if (typeof child !== 'object' || depth >= MAX_SIGNAL_DEPTH) return false;
      stack.push([child, depth + 1]);
    }
  }
  return jsonLength(data) <= MAX_SIGNAL_JSON;
}
