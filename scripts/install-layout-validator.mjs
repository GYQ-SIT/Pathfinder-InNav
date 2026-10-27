import { readFile, readdir, mkdir, cp, rename, rm } from 'node:fs/promises';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const name = '@gyq-sit/innavi-layout-validator';
const consumer = JSON.parse(await readFile(join(root, 'package.json'), 'utf8'));
const pin = consumer.dependencies[name];
if (!/^\d+\.\d+\.\d+$/.test(pin)) throw Error('Plugin dependency must pin an exact semantic version');
const source = resolve(root, 'node_modules', name);
const pkg = JSON.parse(await readFile(join(source, 'package.json'), 'utf8'));
const descriptor = JSON.parse(await readFile(join(source, 'InNaviLayoutValidator.uplugin'), 'utf8'));
if (pkg.version !== pin || descriptor.VersionName !== pin) throw Error('Installed plugin does not match the consumer version pin');
const targetArg = process.argv.indexOf('--destination');
const destination = targetArg < 0 ? join(root, 'InNavi/Plugins/InNaviLayoutValidator') : resolve(process.argv[targetArg + 1]);
const checkOnly = process.argv.includes('--check');
const files = ['InNaviLayoutValidator.uplugin', 'Source', 'README.md', 'CHANGELOG.md', 'Examples', 'package.json'];
async function manifest(base, items = files) {
  const result = {};
  async function visit(relative) {
    const path = join(base, relative);
    const entries = await readdir(path, { withFileTypes: true }).catch(error => {
      if (error.code !== 'ENOTDIR') throw error;
      return null;
    });
    if (entries) for (const entry of entries) await visit(join(relative, entry.name));
    else result[relative.replaceAll('\\', '/')] = createHash('sha256').update(await readFile(path)).digest('hex');
  }
  for (const item of items) await visit(item);
  return result;
}
const expected = await manifest(source);
if (checkOnly) {
  const actual = await manifest(destination);
  if (JSON.stringify(Object.entries(actual).sort()) !== JSON.stringify(Object.entries(expected).sort())) throw Error('Product plugin differs from the pinned registry package');
  console.log(`Verified ${name}@${pin}: ${Object.keys(expected).length} files match the installed source package`);
} else {
  // Replace only package-owned paths; preserve Unreal's local Binaries/Intermediate directories.
  // A temporary sibling and rollback protect a partially copied source tree.
  await mkdir(destination, { recursive: true });
  const stage = destination + '.install-stage';
  await mkdir(stage, { recursive: false });
  const replaced = [];
  try {
    for (const item of files) await cp(join(source, item), join(stage, item), { recursive: true });
    for (const item of files) {
      const old = join(destination, item);
      let existed = true;
      try { await rename(old, join(stage, item + '.previous')); }
      catch (error) { if (error.code === 'ENOENT') existed = false; else throw error; }
      replaced.push({ item, existed });
      await rename(join(stage, item), old);
    }
  } catch (error) {
    for (const {item, existed} of replaced.reverse()) {
      await rm(join(destination, item), { recursive: true, force: true });
      if (existed) await rename(join(stage, item + '.previous'), join(destination, item));
    }
    throw error;
  } finally { await rm(stage, { recursive: true, force: true }); }
  console.log(`Installed ${name}@${pin} into ${destination}`);
}
