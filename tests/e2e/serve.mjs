// Serves the web build locally for playing and debugging: npm run serve
import { startServer } from './harness.mjs';

const { url } = await startServer(8080);
console.log(`Last Stand is running at ${url}`);
