import {initializeApp} from 'firebase/app';
import {getDatabase, type Database} from 'firebase/database';

// Reads are public (rules: ".read": true), so the database URL is the only
// config the dashboard needs — no API key, no login.
const url = import.meta.env.VITE_FIREBASE_DATABASE_URL as string | undefined;

export const databaseUrl = url && url.length > 0 ? url : null;

export const db: Database | null = databaseUrl
  ? getDatabase(initializeApp({databaseURL: databaseUrl}))
  : null;
