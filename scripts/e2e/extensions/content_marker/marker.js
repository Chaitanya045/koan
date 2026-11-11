// Marks the page: a test reads the attribute (CDP) or the text (koan snapshot).
document.documentElement.dataset.koanExtension = 'ran';
const p = document.createElement('p');
p.textContent = 'EXTENSION RAN';
document.body.appendChild(p);
