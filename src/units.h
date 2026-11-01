#pragma once

#include <QDoubleSpinBox>
#include <QList>
#include <QString>

// Length units. Every length in the document is stored in millimetres; these helpers
// only change how a length is shown and how typed text is read.
//
// Typed input accepts any unit whatever the current system: 1200, 1.2m, 120cm, 48", 4',
// 4'-6", 4'6 1/2", 6-1/2", 3/8". A bare number is in the current unit (inches for the
// feet-and-inches systems, like AutoCAD).
namespace Units {

enum System { Millimetre, Centimetre, Metre, Inch, Foot, Architectural, Engineering, SystemCount };

struct Options
{
    int system = Millimetre;
    int decimals = 2;     // decimal units (mm, cm, m, in, ft, engineering inches)
    int fracDen = 16;     // Architectural: round to 1/fracDen inch
};

const Options &options();
void setOptions(const Options &o);

QString systemName(int sys);
bool isImperial(int sys);
bool isImperial();
QString unitLabel();                          // mm, cm, m, in, ft, ft-in
QString format(double mm, bool unitMark = true);
QString formatDim(double mm);                 // dimension text: bare in metric, marked in imperial
QString formatArea(double mm2);
QString formatBoardArea(double mm2);          // sheet material: m² (metric) or ft² (imperial)
bool parse(const QString &text, double &mm);  // also a sum: 800-2*18, (2400-54)/4, 1.2m-18, 4'-6"+3/4"
bool parseNumber(const QString &text, double &v);   // a plain number or sum without units (angles): 360/7, 90+45/2
bool isExpression(const QString &text);       // has an operator, so the field shows "= result"
bool isInputChar(QChar c);                    // characters a typed length may contain
QString inputHint();
QList<double> gridPresets();                  // grid sizes (mm) that suit the current system
double niceStep(double metricStepMm);         // spin box step in the current system

} // namespace Units

// A spin box whose value is in mm but that shows and reads the current units.
// Switching the units system re-labels every LengthSpin (refreshAll).
class LengthSpin : public QDoubleSpinBox
{
public:
    explicit LengthSpin(QWidget *parent = nullptr);
    ~LengthSpin() override;
    void setMetricStep(double mm);
    void refreshUnits();
    static void refreshAll();

protected:
    QString textFromValue(double v) const override;
    double valueFromText(const QString &t) const override;
    QValidator::State validate(QString &input, int &pos) const override;
    void fixup(QString &input) const override;

private:
    double m_metricStep = 1.0;
};
