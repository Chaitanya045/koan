// Parallel version of lib views(): same shape, one burst of style requests.
import { connect } from './lib/koan.mjs';
export async function fviews() {
  const ui = await connect(`ws://127.0.0.1:${process.env.UI_PORT}/0`);
  await ui.send('DOM.enable'); await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const nodes = [];
  const walk = (n, depth, parent, path) => {
    const a = n.attributes ?? []; const cls = a[a.indexOf('class') + 1];
    let me = parent;
    if (depth > 0 && n.nodeName === 'View' && cls) { me = { n, cls, parent, path }; nodes.push(me); path = [...path, cls]; }
    else if (n.nodeName === 'Widget') { me = null; path = []; }
    for (const c of n.children ?? []) walk(c, depth + 1, me, path);
  };
  walk(doc, 0, null, []);
  const styles = await Promise.all(nodes.map(o => ui.send('CSS.getMatchedStylesForNode', { nodeId: o.n.nodeId })));
  nodes.forEach((o, i) => { const p = {}; for (const r of styles[i].result?.matchedCSSRules ?? []) for (const pr of r.rule.style.cssProperties) p[pr.name] = pr.value; o.p = p; });
  const out = nodes.map(o => {
    let shown = true, x = 0, y = 0;
    for (let q = o; q; q = q.parent) { shown = shown && q.p.Visible !== 'false'; x += +q.p.X; y += +q.p.Y; }
    return { cls: o.cls, shown, w: +o.p.Width, h: +o.p.Height, x, y, nodeId: o.n.nodeId, path: o.path, props: o.p };
  });
  ui.close(); return out;
}
