#include "units.h"

#include <QLineEdit>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Units {

static Options g_opt;

const Options &options() { return g_opt; }

void setOptions(const Options &o)
{
    g_opt = o;
    g_opt.system = std::clamp(o.system, 0, int(SystemCount) - 1);
    g_opt.decimals = std::clamp(o.decimals, 0, 6);
    g_opt.fracDen = std::clamp(o.fracDen, 1, 256);
}

QString systemName(int sys)
{
    switch (sys) {
    case Centimetre:    return "Centimetres (cm)";
    case Metre:         return "Metres (m)";
    case Inch:          return "Inches (decimal)";
    case Foot:          return "Feet (decimal)";
    case Architectural: return "Feet-inches, Architectural  5'-6 1/2\"";
    case Engineering:   return "Feet-inches, Engineering  5'-6.50\"";
    default:            return "Millimetres (mm)";
    }
}

bool isImperial(int sys) { return sys >= Inch; }
bool isImperial() { return isImperial(g_opt.system); }

QString unitLabel()
{
    static const char *n[] = {"mm", "cm", "m", "in", "ft", "ft-in", "ft-in"};
    return n[std::clamp(g_opt.system, 0, int(SystemCount) - 1)];
}

// mm per bare typed number
static double bareFactor()
{
    switch (g_opt.system) {
    case Centimetre: return 10.0;
    case Metre:      return 1000.0;
    case Foot:       return 304.8;
    case Inch: case Architectural: case Engineering: return 25.4;
    default:         return 1.0;
    }
}

static QString decimal(double v, int dec)
{
    QString t = QString::number(v, 'f', std::clamp(dec, 0, 8));
    if (t.contains('.')) {
        while (t.endsWith('0')) t.chop(1);
        if (t.endsWith('.')) t.chop(1);
    }
    if (t == "-0")
        t = "0";
    return t;
}

static QString feetInches(double mm, bool engineering)
{
    const double in = std::fabs(mm) / 25.4;
    long long ft = 0;
    bool zero = false;
    QString inch;
    if (engineering) {
        const int dec = std::clamp(g_opt.decimals, 0, 6);
        const double q = std::pow(10.0, dec);
        const long long n = std::llround(in * q), per = std::llround(12.0 * q);
        ft = n / per;
        inch = QString::number(double(n - ft * per) / q, 'f', dec);
        zero = n == 0;
    } else {
        const long long den = std::max(1, g_opt.fracDen);
        const long long n = std::llround(in * den);
        ft = n / (12 * den);
        const long long rem = n - ft * 12 * den, whole = rem / den;
        long long num = rem % den, d = den;
        if (num) {
            const long long g = std::gcd(num, d);
            num /= g;
            d /= g;
        }
        if (!num)
            inch = QString::number(whole);
        else if (!whole)
            inch = QString("%1/%2").arg(num).arg(d);
        else
            inch = QString("%1 %2/%3").arg(whole).arg(num).arg(d);
        zero = n == 0;
    }
    const QString s = ft > 0 ? QString("%1'-%2\"").arg(ft).arg(inch) : inch + "\"";
    return (mm < 0 && !zero) ? "-" + s : s;
}

QString format(double mm, bool unitMark)
{
    switch (g_opt.system) {
    case Centimetre:    return decimal(mm / 10.0, g_opt.decimals) + (unitMark ? " cm" : "");
    case Metre:         return decimal(mm / 1000.0, g_opt.decimals + 1) + (unitMark ? " m" : "");
    case Inch:          return decimal(mm / 25.4, g_opt.decimals) + (unitMark ? "\"" : "");
    case Foot:          return decimal(mm / 304.8, g_opt.decimals) + (unitMark ? "'" : "");
    case Architectural: return feetInches(mm, false);
    case Engineering:   return feetInches(mm, true);
    default:            return decimal(mm, g_opt.decimals) + (unitMark ? " mm" : "");
    }
}

QString formatDim(double mm) { return format(mm, isImperial()); }

QString formatArea(double mm2)
{
    switch (g_opt.system) {
    case Millimetre: return decimal(mm2, g_opt.decimals) + " mm²";
    case Centimetre: return decimal(mm2 / 100.0, g_opt.decimals) + " cm²";
    case Metre:      return decimal(mm2 / 1e6, g_opt.decimals + 2) + " m²";
    case Inch:       return decimal(mm2 / (25.4 * 25.4), g_opt.decimals) + " in²";
    default:         return decimal(mm2 / (304.8 * 304.8), std::max(2, g_opt.decimals)) + " ft²";
    }
}

QString formatBoardArea(double mm2)
{
    if (isImperial())
        return decimal(mm2 / (304.8 * 304.8), 2) + " ft²";
    return decimal(mm2 / 1e6, 3) + " m²";
}

static bool number(const QString &s, double &v)
{
    QString t = s.trimmed();
    t.replace(',', '.');
    if (t.isEmpty())
        return false;
    bool ok = false;
    v = t.toDouble(&ok);
    return ok && std::isfinite(v);
}

// "6"  "6.5"  "1/2"  "6 1/2"  "6-1/2"
static bool mixed(const QString &s, double &v)
{
    const QString t = s.trimmed();
    const int slash = t.indexOf('/');
    if (slash < 0)
        return number(t, v);
    QString whole, frac = t;
    for (int i = slash - 1; i >= 0; --i)
        if (t[i] == ' ' || t[i] == '-') {
            whole = t.left(i).trimmed();
            frac = t.mid(i + 1).trimmed();
            break;
        }
    const int sl = frac.indexOf('/');
    double a = 0, b = 0, w = 0;
    if (!number(frac.left(sl), a) || !number(frac.mid(sl + 1), b) || b == 0)
        return false;
    if (!whole.isEmpty() && !number(whole, w))
        return false;
    v = w + a / b;
    return true;
}

// one length written the usual ways: 1200, 1.2m, 48", 4'-6 1/2", 6-1/2", 3/8"
static bool parseLiteral(const QString &text, double &mm)
{
    QString s = text.trimmed().toLower();
    s.replace(QChar(0x2032), '\'').replace(QChar(0x2019), '\'');
    s.replace(QChar(0x2033), '"').replace(QChar(0x201D), '"').replace("''", "\"");
    for (const char *w : {"feet", "foot", "ft"})
        s.replace(QString::fromLatin1(w), QStringLiteral("'"));
    for (const char *w : {"inches", "inch", "in"})
        s.replace(QString::fromLatin1(w), QStringLiteral("\""));
    s = s.simplified();
    if (s.isEmpty())
        return false;
    bool neg = false;
    if (s.startsWith('-') || s.startsWith('+')) {
        neg = s.startsWith('-');
        s = s.mid(1).trimmed();
    }
    double v = 0;
    const int ft = s.indexOf('\'');
    if (ft >= 0) {
        double feet = 0, inch = 0;
        if (!mixed(s.left(ft), feet))
            return false;
        QString rest = s.mid(ft + 1).trimmed();
        if (rest.startsWith('-'))
            rest = rest.mid(1).trimmed();
        if (rest.endsWith('"'))
            rest.chop(1);
        if (!rest.isEmpty() && !mixed(rest, inch))
            return false;
        v = feet * 304.8 + inch * 25.4;
    } else if (s.endsWith('"')) {
        double inch = 0;
        if (!mixed(s.left(s.size() - 1), inch))
            return false;
        v = inch * 25.4;
    } else {
        double f = bareFactor();
        if (s.endsWith("mm")) { f = 1.0; s.chop(2); }
        else if (s.endsWith("cm")) { f = 10.0; s.chop(2); }
        else if (s.endsWith('m')) { f = 1000.0; s.chop(1); }
        double x = 0;
        if (!mixed(s, x))
            return false;
        v = x * f;
    }
    mm = neg ? -v : v;
    return std::isfinite(mm);
}

// ---------------------------------------------------------------------------
// the calculator in every typed value: + - * / ( ) with lengths in any unit, e.g. 800-2*18,
// (2400-3*18)/4, 1.2m-18, 4'-6"+3/4". Lengths are worked out in the current unit, so a bare
// number is both a factor (2*18) and a length (18).
// ---------------------------------------------------------------------------
namespace {
struct Calc
{
    QString s;
    int i = 0;
    bool length = true;

    void ws() { while (i < s.size() && s[i] == QLatin1Char(' ')) ++i; }
    bool isOp(QChar c) const { return QStringLiteral("+-*/()").contains(c); }

    // the longest piece from i on that reads as one length and is followed by an operator or the end
    bool literal(double &v)
    {
        ws();
        if (i >= s.size() || !(s[i].isDigit() || s[i] == QLatin1Char('.')))
            return false;
        const bool imp = isImperial();
        for (int j = int(s.size()); j > i; --j) {
            const QString sub = s.mid(i, j - i).trimmed();
            if (sub.isEmpty() || sub.contains(QLatin1Char('+')) || sub.contains(QLatin1Char('*')) ||
                sub.contains(QLatin1Char('(')) || sub.contains(QLatin1Char(')')))
                continue;
            // metric: - / and spaces are operators, except inside a value written in feet and inches (4'-6 1/2");
            // imperial keeps 6-1/2 and 6 1/2 as one value
            const bool feetInch = sub.contains(QLatin1Char('\'')) || sub.contains(QLatin1Char('"'));
            if (!imp && !feetInch &&
                (sub.contains(QLatin1Char('-')) || sub.contains(QLatin1Char('/')) || sub.contains(QLatin1Char(' '))))
                continue;
            int k = j;
            while (k < s.size() && s[k] == QLatin1Char(' ')) ++k;
            if (k < s.size() && !isOp(s[k]))
                continue;
            double x = 0;
            if (length) {
                if (!parseLiteral(sub, x))
                    continue;
                x /= bareFactor();
            } else {
                bool ok = false;
                x = sub.toDouble(&ok);
                if (!ok)
                    continue;
            }
            v = x;
            i = j;
            return true;
        }
        return false;
    }
    bool factor(double &v)
    {
        ws();
        if (i < s.size() && (s[i] == QLatin1Char('-') || s[i] == QLatin1Char('+'))) {
            const bool neg = s[i++] == QLatin1Char('-');
            if (!factor(v))
                return false;
            if (neg) v = -v;
            return true;
        }
        if (i < s.size() && s[i] == QLatin1Char('(')) {
            ++i;
            if (!expr(v))
                return false;
            ws();
            if (i >= s.size() || s[i] != QLatin1Char(')'))
                return false;
            ++i;
            return true;
        }
        return literal(v);
    }
    bool term(double &v)
    {
        if (!factor(v))
            return false;
        for (;;) {
            ws();
            if (i >= s.size() || (s[i] != QLatin1Char('*') && s[i] != QLatin1Char('/')))
                return true;
            const bool mul = s[i++] == QLatin1Char('*');
            double r = 0;
            if (!factor(r) || (!mul && std::fabs(r) < 1e-300))
                return false;
            v = mul ? v * r : v / r;
        }
    }
    bool expr(double &v)
    {
        if (!term(v))
            return false;
        for (;;) {
            ws();
            if (i >= s.size() || (s[i] != QLatin1Char('+') && s[i] != QLatin1Char('-')))
                return true;
            const bool add = s[i++] == QLatin1Char('+');
            double r = 0;
            if (!term(r))
                return false;
            v = add ? v + r : v - r;
        }
    }
};

QString calcText(const QString &text)
{
    QString s = text.trimmed().toLower();
    s.replace(QChar(0x2032), QLatin1Char('\'')).replace(QChar(0x2019), QLatin1Char('\''));
    s.replace(QChar(0x2033), QLatin1Char('"')).replace(QChar(0x201D), QLatin1Char('"')).replace(QStringLiteral("''"), QStringLiteral("\""));
    s.replace(QChar(0x00D7), QLatin1Char('*')).replace(QChar(0x00F7), QLatin1Char('/')).replace(QChar(0x2212), QLatin1Char('-'));
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    return s.simplified();
}

bool calc(const QString &text, double &v, bool length)
{
    Calc c;
    c.s = calcText(text);
    c.length = length;
    if (c.s.isEmpty() || !c.expr(v))
        return false;
    c.ws();
    if (c.i != c.s.size() || !std::isfinite(v))
        return false;
    if (length)
        v *= bareFactor();
    return true;
}
} // namespace

bool parse(const QString &text, double &mm)
{
    // feet-inch values keep their own forms first (6-1/2" is six and a half inches, not 5.5)
    if (isImperial() && parseLiteral(text, mm))
        return true;
    return calc(text, mm, true);
}

bool parseNumber(const QString &text, double &v)
{
    return calc(text, v, false);
}

bool isExpression(const QString &text)
{
    const QString s = calcText(text);
    for (int i = 1; i < s.size(); ++i)
        if (QStringLiteral("+*()").contains(s[i]) || (s[i] == QLatin1Char('-') && !isImperial()) ||
            (s[i] == QLatin1Char('/') && !isImperial()))
            return true;
    return s.startsWith(QLatin1Char('('));
}

bool isInputChar(QChar c)
{
    return c.isDigit() || QString(".,-+'\"/ mcinfteohs*()").contains(c.toLower()) || c == QChar(0x2032) ||
           c == QChar(0x2033) || c == QChar(0x00D7);
}

QString inputHint()
{
    if (isImperial())
        return g_opt.system == Foot ? "bare number = feet; 4'-6 1/2\", 54\", 1.2m also work"
                                    : "bare number = inches; type 4'-6 1/2\", 4' 6\", 3/8\" or 1.2m";
    return QString("bare number = %1; 1.2m, 120cm, 48\" or 4'-6\" also work; sums too: 800-2*18").arg(unitLabel());
}

QList<double> gridPresets()
{
    if (isImperial())
        return {25.4 / 16, 25.4 / 8, 25.4 / 4, 25.4 / 2, 25.4, 50.8, 76.2, 152.4, 304.8, 609.6, 1524.0};
    return {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 250.0, 500.0, 1000.0};
}

double niceStep(double metricStepMm)
{
    if (!isImperial())
        return metricStepMm;
    static const double inches[] = {1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 6.0, 12.0};
    const double want = std::max(1e-6, metricStepMm / 25.4);
    double best = inches[0];
    for (double c : inches)
        if (std::fabs(std::log(c / want)) < std::fabs(std::log(best / want)))
            best = c;
    return best * 25.4;
}

} // namespace Units

// ---------------------------------------------------------------------------
static QList<LengthSpin *> &spinRegistry()
{
    static QList<LengthSpin *> r;
    return r;
}

LengthSpin::LengthSpin(QWidget *parent) : QDoubleSpinBox(parent)
{
    setDecimals(6);                    // keep inch values exact; the text shows the unit's own precision
    setKeyboardTracking(false);
    spinRegistry().append(this);
}

LengthSpin::~LengthSpin()
{
    spinRegistry().removeAll(this);
}

void LengthSpin::setMetricStep(double mm)
{
    m_metricStep = mm;
    setSingleStep(Units::niceStep(mm));
}

void LengthSpin::refreshUnits()
{
    setSingleStep(Units::niceStep(m_metricStep));
    setRange(minimum(), maximum());    // drops the cached size hint
    lineEdit()->setText(value() == minimum() && !specialValueText().isEmpty() ? specialValueText()
                                                                               : textFromValue(value()));
    updateGeometry();
}

void LengthSpin::refreshAll()
{
    for (LengthSpin *s : std::as_const(spinRegistry()))
        s->refreshUnits();
}

QString LengthSpin::textFromValue(double v) const
{
    return Units::format(v, true);
}

double LengthSpin::valueFromText(const QString &t) const
{
    if (!specialValueText().isEmpty() && t.trimmed() == specialValueText())
        return minimum();
    double mm = 0;
    return Units::parse(t, mm) ? mm : value();
}

QValidator::State LengthSpin::validate(QString &input, int &) const
{
    if (!specialValueText().isEmpty() && input.trimmed() == specialValueText())
        return QValidator::Acceptable;
    double mm = 0;
    if (Units::parse(input, mm))
        return (mm >= minimum() - 1e-9 && mm <= maximum() + 1e-9) ? QValidator::Acceptable : QValidator::Intermediate;
    for (const QChar c : std::as_const(input))
        if (!Units::isInputChar(c))
            return QValidator::Invalid;
    return QValidator::Intermediate;
}

void LengthSpin::fixup(QString &input) const
{
    double mm = 0;
    if (Units::parse(input, mm))
        input = textFromValue(std::clamp(mm, minimum(), maximum()));
}
