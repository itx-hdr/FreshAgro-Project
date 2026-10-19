# Vercel frontend

This static dashboard is the deployed production frontend: https://freshagro-bice.vercel.app/

Deploy this folder as a Vercel project (framework preset: Other, build command empty, output directory `.`). `config.js` contains only the public Supabase URL and publishable key. Never put the device ingestion token or Supabase service-role key in this folder.
