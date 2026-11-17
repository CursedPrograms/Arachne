// Arachne's page: the feature cards and the year
(function () {
  'use strict';
  const features = [
    ['Its own tracer', 'Colours found in Lab, speckles merged, shapes stacked with no hairline gaps, holes kept, corners kept, smooth Bezier curves fitted. A fraction of a second a picture.'],
    ['Paint from words', 'Describe it, pick a style (flat illustration, logo, line art, sticker, icon): Krea 2 Turbo paints it on your graphics card, then it is traced.'],
    ['Every setting a slider', 'Colours, speckle, smoothing, straighten, corners, curve fit, detail, background, outlines: each a slider, the drawing redone as you move it.'],
    ['See what it did', 'The picture, the vector art, or both with a wipe to drag between them, and the colours it used.'],
    ['SVG, PNG or the clipboard', 'Save the SVG, a PNG at any size (vector art is sharp at any size), or copy the SVG and paste it anywhere.'],
    ['Twelve languages', 'English, Japanese, Chinese, Spanish, Korean, French, German, Russian, Serbian, Afrikaans, Yucatec Maya and Pennsylvania Dutch.']
  ];
  const grid = document.getElementById('features');
  if (grid) {
    for (const [title, text] of features) {
      const card = document.createElement('article');
      const h = document.createElement('h2');
      h.textContent = title;
      const p = document.createElement('p');
      p.textContent = text;
      card.append(h, p);
      grid.appendChild(card);
    }
  }
  const year = document.getElementById('year');
  if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
})();
