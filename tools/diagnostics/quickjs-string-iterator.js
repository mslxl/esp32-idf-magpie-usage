// Evaluate in the target PocketJS guest: 0 means every iterator terminated.
// This bounded reproduction returned 15 before the Xtensa cursor fix and 0 after.
// Host JS engines cannot reproduce the target C aliasing defect.
(() => {
  let mask = 0;
  const samples = ["abc", "中文", "a😀b", "Account: " + String(123)];
  for (let sample = 0; sample < samples.length; ++sample) {
    const text = samples[sample];
    const iterator = text[Symbol.iterator]();
    let done = false;
    for (let index = 0; index <= text.length; ++index) {
      if (iterator.next().done) {
        done = true;
        break;
      }
    }
    if (!done) mask |= 1 << sample;
  }
  return mask;
})();
