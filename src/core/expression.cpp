#include <solidara/model.hpp>

#include <QSet>
#include <cmath>
#include <stdexcept>

namespace solidara {
namespace {

struct Quantity {
    double value;
    int lengthPower = 0;
};

[[noreturn]] void expressionError(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

bool unit(const QString& name, double& scale) {
    if (name == "mm") scale = 1;
    else if (name == "cm") scale = 10;
    else if (name == "m") scale = 1000;
    else if (name == "in") scale = 25.4;
    else return false;
    return true;
}

class Parser {
public:
    Parser(QString expression, const QMap<QString, QString>& parameters, QSet<QString>& active, QMap<QString, Quantity>& cache)
        : text_(std::move(expression)), parameters_(parameters), active_(active), cache_(cache) {}

    Quantity evaluate() {
        if (text_.size() > 512) expressionError("Expression exceeds 512 characters.");
        Quantity result = sum();
        whitespace();
        if (position_ != text_.size()) expressionError("Unexpected text in expression: " + text_.mid(position_));
        if (!std::isfinite(result.value)) expressionError("Expression produced a non-finite value.");
        return result;
    }

private:
    void whitespace() {
        while (position_ < text_.size() && text_[position_].isSpace()) ++position_;
    }

    bool consume(QChar character) {
        whitespace();
        if (position_ < text_.size() && text_[position_] == character) {
            ++position_;
            return true;
        }
        return false;
    }

    Quantity sum() {
        Quantity left = product();
        while (true) {
            const bool addition = consume('+');
            if (!addition && !consume('-')) break;
            const Quantity right = product();
            if (left.lengthPower != right.lengthPower)
                expressionError("Addition and subtraction require matching units; use an explicit unit such as 2 mm.");
            left.value += (addition ? right.value : -right.value);
        }
        return left;
    }

    Quantity product() {
        Quantity left = atom();
        while (true) {
            const bool multiplication = consume('*');
            if (!multiplication && !consume('/')) break;
            const Quantity right = atom();
            if (!multiplication && right.value == 0) expressionError("Division by zero.");
            left.value = multiplication ? left.value * right.value : left.value / right.value;
            left.lengthPower += multiplication ? right.lengthPower : -right.lengthPower;
            if (std::abs(left.lengthPower) > 16) expressionError("Expression has unsupported dimensional complexity.");
        }
        return left;
    }

    Quantity atom() {
        if (++depth_ > 64) expressionError("Expression nesting exceeds 64 levels.");
        const auto result = atomValue();
        --depth_;
        return result;
    }

    Quantity atomValue() {
        if (consume('+')) return atom();
        if (consume('-')) {
            Quantity value = atom();
            value.value = -value.value;
            return value;
        }
        if (consume('(')) {
            Quantity value = sum();
            if (!consume(')')) expressionError("Missing closing parenthesis.");
            return value;
        }
        whitespace();
        if (position_ == text_.size()) expressionError("Expected a number, parameter, or parenthesized expression.");
        if (text_[position_].isDigit() || text_[position_] == '.') {
            const qsizetype start = position_;
            while (position_ < text_.size() && (text_[position_].isDigit() || text_[position_] == '.')) ++position_;
            if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
                ++position_;
                if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
                while (position_ < text_.size() && text_[position_].isDigit()) ++position_;
            }
            bool ok = false;
            Quantity value{text_.mid(start, position_ - start).toDouble(&ok)};
            if (!ok) expressionError("Invalid number in expression.");
            whitespace();
            const qsizetype unitStart = position_;
            const QString name = identifier();
            double scale = 1;
            if (!name.isEmpty()) {
                if (!unit(name, scale)) expressionError("Unknown unit: " + name);
                value.value *= scale;
                value.lengthPower = 1;
            } else {
                position_ = unitStart;
            }
            return value;
        }
        const QString name = identifier();
        if (name.isEmpty()) expressionError("Invalid character in expression.");
        double scale = 1;
        if (unit(name, scale)) return {scale, 1};
        if (!parameters_.contains(name)) expressionError("Unknown parameter: " + name);
        if (cache_.contains(name)) return cache_.value(name);
        if (active_.contains(name)) expressionError("Circular parameter dependency: " + name);
        if (active_.size() >= 64) expressionError("Parameter dependency chain exceeds 64 levels.");
        active_.insert(name);
        const Quantity value = Parser(parameters_.value(name), parameters_, active_, cache_).evaluate();
        active_.remove(name);
        cache_.insert(name, value);
        return value;
    }

    QString identifier() {
        if (position_ >= text_.size() || (!text_[position_].isLetter() && text_[position_] != '_')) return {};
        const qsizetype start = position_++;
        while (position_ < text_.size() && (text_[position_].isLetterOrNumber() || text_[position_] == '_')) ++position_;
        return text_.mid(start, position_ - start);
    }

    QString text_;
    const QMap<QString, QString>& parameters_;
    QSet<QString>& active_;
    QMap<QString, Quantity>& cache_;
    qsizetype position_ = 0;
    int depth_ = 0;
};

} // namespace

double evaluateLength(const QString& expression, const QMap<QString, QString>& parameters) {
    QSet<QString> active;
    QMap<QString, Quantity> cache;
    const Quantity value = Parser(expression, parameters, active, cache).evaluate();
    // Unadorned numbers use the document's fixed millimeter unit. Explicit units
    // remain dimension-checked so an area cannot be used as an extrusion depth.
    if (value.lengthPower != 0 && value.lengthPower != 1)
        expressionError("A length expression is required, not an area or inverse length.");
    return value.value;
}

} // namespace solidara
