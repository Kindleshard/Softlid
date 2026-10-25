using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace BlinkBuddy
{
    // When the blue light filter should be on: sunset to sunrise where you are (from Windows location, or guessed from
    // the time zone; computed offline), or fixed hours. It eases in over the fade time after the start and out over the
    // fade time before the end.
    static class Night
    {
        // Asks Windows where this PC is (no prompt; it answers only if location is allowed for desktop apps), rounded
        // to about a kilometre, and calls done(lat, lon) on a pool thread, or done(NaN, NaN) if it can't tell.
        public static void Locate(Action<double, double> done)
        {
            System.Threading.ThreadPool.QueueUserWorkItem(delegate
            {
                double lat = double.NaN, lon = double.NaN;
                try
                {
                    using (var watcher = new System.Device.Location.GeoCoordinateWatcher(System.Device.Location.GeoPositionAccuracy.Default))
                    {
                        if (watcher.TryStart(true, TimeSpan.FromSeconds(15)))
                        {
                            // Without GPS, Windows works it out from Wi-Fi, which can take a while after starting.
                            for (int i = 0; i < 60 && watcher.Position.Location.IsUnknown; i++) System.Threading.Thread.Sleep(500);
                            System.Device.Location.GeoCoordinate at = watcher.Position.Location;
                            if (!at.IsUnknown)
                            {
                                lat = Math.Round(at.Latitude, 2);
                                lon = Math.Round(at.Longitude, 2);
                            }
                        }
                        watcher.Stop();
                    }
                }
                catch { }
                done(lat, lon);
            });
        }

        public static string Coordinates(double lat, double lon)
        {
            return lat.ToString("0.00", CultureInfo.InvariantCulture) + ", " + lon.ToString("0.00", CultureInfo.InvariantCulture);
        }

        static double FadeMinutes(Settings s, double len) { return Math.Min(s.NightFade, len / 2); }

        // Name|latitude|longitude|Windows time zone used to guess the city when none is typed
        const string CityData = @"Istanbul|41.01|28.98|Turkey Standard Time
Ankara|39.93|32.86|
Izmir|38.42|27.14|
Bursa|40.19|29.06|
Antalya|36.90|30.70|
Adana|37.00|35.32|
Konya|37.87|32.48|
Gaziantep|37.07|37.38|
Kayseri|38.72|35.48|
Eskisehir|39.78|30.52|
Samsun|41.29|36.33|
Trabzon|41.00|39.72|
Diyarbakir|37.91|40.24|
Erzurum|39.90|41.27|
London|51.51|-0.13|GMT Standard Time
Dublin|53.35|-6.26|
Lisbon|38.72|-9.14|
Madrid|40.42|-3.70|
Barcelona|41.39|2.17|
Paris|48.86|2.35|Romance Standard Time
Brussels|50.85|4.35|
Amsterdam|52.37|4.90|
Berlin|52.52|13.40|W. Europe Standard Time
Munich|48.14|11.58|
Hamburg|53.55|9.99|
Frankfurt|50.11|8.68|
Zurich|47.38|8.54|
Vienna|48.21|16.37|
Rome|41.90|12.50|
Milan|45.46|9.19|
Prague|50.08|14.44|
Warsaw|52.23|21.01|Central European Standard Time
Budapest|47.50|19.04|Central Europe Standard Time
Stockholm|59.33|18.07|
Oslo|59.91|10.75|
Copenhagen|55.68|12.57|
Helsinki|60.17|24.94|
Athens|37.98|23.73|GTB Standard Time
Sofia|42.70|23.32|
Bucharest|44.43|26.10|
Belgrade|44.79|20.45|
Kyiv|50.45|30.52|FLE Standard Time
Moscow|55.76|37.62|Russian Standard Time
Baku|40.41|49.87|Azerbaijan Standard Time
Tbilisi|41.72|44.78|Georgian Standard Time
Nicosia|35.19|33.38|
Tel Aviv|32.09|34.78|Israel Standard Time
Cairo|30.04|31.24|Egypt Standard Time
Riyadh|24.71|46.68|Arab Standard Time
Dubai|25.20|55.27|Arabian Standard Time
Tehran|35.69|51.39|Iran Standard Time
Karachi|24.86|67.01|Pakistan Standard Time
Delhi|28.61|77.21|India Standard Time
Mumbai|19.08|72.88|
Bangalore|12.97|77.59|
Dhaka|23.81|90.41|Bangladesh Standard Time
Bangkok|13.76|100.50|SE Asia Standard Time
Singapore|1.35|103.82|Singapore Standard Time
Jakarta|-6.21|106.85|
Manila|14.60|120.98|
Hong Kong|22.32|114.17|
Beijing|39.90|116.40|China Standard Time
Shanghai|31.23|121.47|
Seoul|37.57|126.98|Korea Standard Time
Tokyo|35.68|139.69|Tokyo Standard Time
Sydney|-33.87|151.21|AUS Eastern Standard Time
Melbourne|-37.81|144.96|
Perth|-31.95|115.86|W. Australia Standard Time
Auckland|-36.85|174.76|New Zealand Standard Time
Johannesburg|-26.20|28.05|South Africa Standard Time
Nairobi|-1.29|36.82|E. Africa Standard Time
Lagos|6.52|3.38|W. Central Africa Standard Time
Toronto|43.65|-79.38|
Montreal|45.50|-73.57|
Vancouver|49.28|-123.12|
New York|40.71|-74.01|Eastern Standard Time
Boston|42.36|-71.06|
Washington|38.91|-77.04|
Miami|25.76|-80.19|
Atlanta|33.75|-84.39|
Chicago|41.88|-87.63|Central Standard Time
Dallas|32.78|-96.80|
Houston|29.76|-95.37|
Denver|39.74|-104.99|Mountain Standard Time
Phoenix|33.45|-112.07|US Mountain Standard Time
Los Angeles|34.05|-118.24|Pacific Standard Time
San Francisco|37.77|-122.42|
Seattle|47.61|-122.33|
Mexico City|19.43|-99.13|Central Standard Time (Mexico)
Bogota|4.71|-74.07|SA Pacific Standard Time
Lima|-12.05|-77.04|
Santiago|-33.45|-70.67|Pacific SA Standard Time
Buenos Aires|-34.60|-58.38|Argentina Standard Time
Sao Paulo|-23.55|-46.63|E. South America Standard Time
Rio de Janeiro|-22.91|-43.17|";

        struct City { public string Name, Key, Tz; public double Lat, Lon; }
        static City[] cities;
        static string cachedText;
        static bool cachedOk;
        static double cachedLat, cachedLon;
        static string cachedName;

        static City[] Cities
        {
            get
            {
                if (cities != null) return cities;
                var list = new List<City>();
                foreach (string line in CityData.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    string[] f = line.Split('|');
                    list.Add(new City
                    {
                        Name = f[0], Key = Simplify(f[0]), Tz = f[3],
                        Lat = double.Parse(f[1], CultureInfo.InvariantCulture),
                        Lon = double.Parse(f[2], CultureInfo.InvariantCulture)
                    });
                }
                return cities = list.ToArray();
            }
        }

        // Cities matching what's typed (ignoring case and accents): those starting with it first, then those
        // containing it, at most max.
        public static List<string> FindCities(string text, int max)
        {
            string key = Simplify(text ?? "");
            var starts = new List<string>();
            var contains = new List<string>();
            if (key.Length == 0) return starts;
            foreach (City c in Cities)
            {
                if (c.Key.StartsWith(key)) starts.Add(c.Name);
                else if (c.Key.Contains(key)) contains.Add(c.Name);
            }
            starts.AddRange(contains);
            if (starts.Count > max) starts.RemoveRange(max, starts.Count - max);
            return starts;
        }

        public static string[] CityNames
        {
            get
            {
                var names = new string[Cities.Length];
                for (int i = 0; i < names.Length; i++) names[i] = Cities[i].Name;
                return names;
            }
        }

        // 1 = full filter strength, 0 = off.
        // The blue light filter strength (%) at a point of the night (0 day .. 1 full night): the schedule's own night
        // strength takes over from the daytime one as the evening eases in, and hands back in the morning. It only
        // ever adds warmth: a night strength below the daytime one leaves the daytime one as it is.
        public static double Blue(Settings s, float night)
        {
            return s.BlueFilter + Math.Max(0, s.NightBlue - s.BlueFilter) * night;
        }

        public static float Factor(Settings s, DateTime now)
        {
            double start, end, into, len;
            string where;
            int state = Window(s, now, out start, out end, out where);
            if (state == -1 || state == 2) return 1f;   // unknown place: keep the filter as set
            if (state == 1) return 0f;
            Position(now, start, end, out into, out len);
            if (into >= len) return 0f;
            double fade = FadeMinutes(s, len);
            if (fade <= 0) return 1f;
            // Smoothstep, so the change starts and ends gently instead of at a constant rate.
            double t = Math.Min(1, Math.Min(into, len - into) / fade);
            return (float)(t * t * (3 - 2 * t));
        }

        public static string Status(Settings s, DateTime now)
        {
            if (!s.ScheduleEnabled) return Lang.T("Off: the blue light filter stays as set on the Screen tab.");
            double start, end, into, len;
            string where;
            int state = Window(s, now, out start, out end, out where);
            if (state == -1)
                return Lang.T("Windows location is off and your time zone isn't one BlinkBuddy knows, so it can't tell when the sun sets. " +
                       "Turn on location for desktop apps in Windows Settings, or use custom hours.");
            if (state == 1) return Lang.F("The sun doesn't set today in {0}, so the filter rests.", where);
            if (state == 2) return Lang.F("The sun doesn't rise today in {0}, so the filter stays on.", where);

            string head = s.ScheduleMode == 0
                ? Lang.F("Sunset {0}, sunrise {1} in {2}.", Hm(start), Hm(end), where)
                : Lang.F("On from {0} to {1}.", Hm(start), Hm(end));
            Position(now, start, end, out into, out len);
            double fade = FadeMinutes(s, len);
            string state2 = into >= len ? Lang.F("Resting until {0}.", Hm(start))
                : into < fade ? Lang.F("Easing in: {0:0}% of the way, full at {1}.", Factor(s, now) * 100, Hm(start + fade))
                : len - into < fade ? Lang.F("Easing out: back to normal at {0}.", Hm(end))
                : Lang.F("Full strength until {0}, then easing out until {1}.", Hm(end - fade), Hm(end));
            return head + "\n" + state2;
        }

        public static string Hm(double minutes)
        {
            int m = (int)Math.Round(Mod(minutes, 1440)) % 1440;
            return string.Format("{0:00}:{1:00}", m / 60, m % 60);
        }

        static void Position(DateTime now, double start, double end, out double into, out double len)
        {
            len = Mod(end - start, 1440);
            into = Mod(now.TimeOfDay.TotalMinutes - start, 1440);
        }

        // Night window in minutes after local midnight. Returns -1 unknown place, 0 normal, 1 no night today, 2 no day today.
        static int Window(Settings s, DateTime now, out double start, out double end, out string where)
        {
            start = s.NightFrom;
            end = s.NightTo;
            where = null;
            if (s.ScheduleMode == 1) return 0;
            double lat, lon;
            if (!Resolve(s.City, out lat, out lon, out where)) return -1;
            return SunTimes(now, lat, lon, out end, out start);   // night runs from sunset to sunrise
        }

        // NOAA's approximation, accurate to a minute or two. Times are local minutes after midnight.
        static int SunTimes(DateTime day, double lat, double lon, out double rise, out double set)
        {
            rise = set = 0;
            double g = 2 * Math.PI / 365 * (day.DayOfYear - 1);
            double eq = 229.18 * (0.000075 + 0.001868 * Math.Cos(g) - 0.032077 * Math.Sin(g) - 0.014615 * Math.Cos(2 * g) - 0.040849 * Math.Sin(2 * g));
            double decl = 0.006918 - 0.399912 * Math.Cos(g) + 0.070257 * Math.Sin(g) - 0.006758 * Math.Cos(2 * g)
                        + 0.000907 * Math.Sin(2 * g) - 0.002697 * Math.Cos(3 * g) + 0.00148 * Math.Sin(3 * g);
            double phi = lat * Math.PI / 180;
            double cosHa = Math.Cos(90.833 * Math.PI / 180) / (Math.Cos(phi) * Math.Cos(decl)) - Math.Tan(phi) * Math.Tan(decl);
            if (cosHa < -1) return 1;
            if (cosHa > 1) return 2;
            double ha = Math.Acos(cosHa) * 180 / Math.PI;
            double offset = TimeZoneInfo.Local.GetUtcOffset(day.Date.AddHours(12)).TotalMinutes;
            rise = Mod(720 - 4 * (lon + ha) - eq + offset, 1440);
            set = Mod(720 - 4 * (lon - ha) - eq + offset, 1440);
            return 0;
        }

        // The place the sun times are worked out for, plainly named (null if none): your Windows location, or the city
        // of your time zone.
        public static string Place(Settings s, out bool fromWindows)
        {
            string text = (s.City ?? "").Trim();
            double lat, lon;
            string[] parts = text.Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries);
            fromWindows = parts.Length == 2 && double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out lat)
                && double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out lon);
            if (fromWindows) return null;
            string key = Simplify(text), tz = TimeZoneInfo.Local.Id;
            foreach (City c in Cities)
                if (text.Length == 0 ? c.Tz == tz : c.Key == key) return c.Name;
            return null;
        }

        static bool Resolve(string text, out double lat, out double lon, out string name)
        {
            text = (text ?? "").Trim();
            if (text != cachedText)
            {
                cachedText = text;
                cachedOk = Lookup(text, out cachedLat, out cachedLon, out cachedName);
            }
            lat = cachedLat;
            lon = cachedLon;
            name = cachedName;
            return cachedOk;
        }

        static bool Lookup(string text, out double lat, out double lon, out string name)
        {
            lat = lon = 0;
            name = null;
            if (text.Length == 0)
            {
                string tz = TimeZoneInfo.Local.Id;
                foreach (City c in Cities)
                    if (c.Tz == tz) { lat = c.Lat; lon = c.Lon; name = Lang.F("{0} (from your time zone)", c.Name); return true; }
                return false;
            }
            string[] parts = text.Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2
                && double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out lat)
                && double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out lon)
                && Math.Abs(lat) <= 90 && Math.Abs(lon) <= 180)
            {
                name = Lang.T("your location (from Windows)");
                return true;
            }
            lat = lon = 0;
            string key = Simplify(text);
            foreach (City c in Cities)
                if (c.Key == key) { lat = c.Lat; lon = c.Lon; name = c.Name; return true; }
            return false;
        }

        // Case- and accent-insensitive, so "İzmir", "izmir" and "Izmir" all match.
        static string Simplify(string s)
        {
            var sb = new StringBuilder();
            foreach (char c in s.Trim().ToLowerInvariant().Replace('ı', 'i').Normalize(NormalizationForm.FormD))
                if (CharUnicodeInfo.GetUnicodeCategory(c) != UnicodeCategory.NonSpacingMark) sb.Append(c);
            return sb.ToString();
        }

        static double Mod(double a, double m) { return ((a % m) + m) % m; }
    }
}
