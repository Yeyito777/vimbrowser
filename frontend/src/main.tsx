import React from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

const tabs = [
  'example.com/',
  'github.com/yeyito/vimbrowser',
  'localhost:5173/',
];

function App() {
  return (
    <main className="shell">
      <aside className="sidebar" aria-label="tab sidebar">
        {tabs.map((url, index) => (
          <div className={`tab-row ${index === 0 ? 'active' : ''}`} key={url}>
            <span className="marker">{index === 0 ? '>' : ' '}</span>
            <span>{index + 1}: {url}</span>
          </div>
        ))}
      </aside>
      <section className="page">
        <article className="example-card">
          <h1>Example Domain</h1>
          <p>This page exists so chrome changes can be mocked quickly in Vite.</p>
          <a href="https://example.com">Learn more</a>
        </article>
      </section>
      <section className="command-line" aria-label="command line">
        <div className="command-top-border" />
        <div className="command-input">:open example.com<span className="caret" /></div>
      </section>
    </main>
  );
}

createRoot(document.getElementById('root')!).render(<App />);
