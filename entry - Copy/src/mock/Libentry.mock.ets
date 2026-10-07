const NativeMock: Record<string, Object> = {
  'setEnvironment': (_density: number, _region: string, _is24Hour: boolean) => {
  },
  'setResourceManager': (_resmgr: Object) => {
  },
  'setNavigationHandler': (_handler: (destination: string) => void) => {
  },
  'registerFont': (_font: ArrayBuffer) => {
    return false;
  },
  'setRefreshHandler': (_handler: (locationIndex: number) => void) => {
  },
  'setOrderHandler': (_handler: (order: string) => void) => {
  },
  'endRefresh': (_xcomponentId: string) => {
  },
  'alertFeedLinks': (_rss: string) => {
    return [] as string[];
  },
  'applyAlerts': (
    _xcomponentId: string,
    _locationIndex: number,
    _region: string,
    _capDocuments: string
  ) => {
  },
  'restoreCardOrder': (_xcomponentId: string, _order: string) => {
  },
  'applyForecast': (
    _xcomponentId: string,
    _locationIndex: number,
    _locationCount: number,
    _city: string,
    _body: string,
    _airQualityBody: string,
    _latitude: number,
    _longitude: number,
    _darkMode: boolean
  ) => {
    // There is no surface under a mock, so the call is a no-op.
  },
};

export default NativeMock;
