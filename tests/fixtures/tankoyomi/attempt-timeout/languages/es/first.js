var TankoyomiProvider = {
  searchSeries: function(ctx, query) {
    return ctx.fetchJson('https://93.184.216.34/first/search');
  },
  getChapters: function(ctx, series) {
    return ctx.fetchJson('https://93.184.216.34/first/chapters');
  },
  getPages: function(ctx, chapter) { return []; }
};
