#pragma once
#include <QString>
#include <stdexcept>
#include <utility>
#include <vector>

namespace levelcad::app::sexpr {
struct Expression {
    QString atom;
    std::vector<Expression> children;

    QString tag() const {
        return children.empty() ? QString() : children.front().atom;
    }

    QString value(int index = 1) const {
        return index < static_cast<int>(children.size()) ? children[index].atom : QString();
    }

    const Expression* child(const QString& name) const {
        for (const auto& item : children)
            if (!item.children.empty() && item.tag() == name)
                return &item;
        return nullptr;
    }
};

class Parser {
  public:
    explicit Parser(QString text) : text_(std::move(text)) {}

    Expression read(int depth = 0) {
        if (depth > 100)
            throw std::runtime_error("KiCad expression nesting is too deep");
        whitespace();
        if (position_ >= text_.size())
            throw std::runtime_error("Unexpected end of KiCad library");
        Expression expression;
        if (text_[position_] == '(') {
            ++position_;
            whitespace();
            while (position_ < text_.size() && text_[position_] != ')') {
                expression.children.push_back(read(depth + 1));
                whitespace();
            }
            if (position_ >= text_.size())
                throw std::runtime_error("Unclosed KiCad expression");
            ++position_;
        } else if (text_[position_] == '"') {
            ++position_;
            bool closed = false;
            while (position_ < text_.size()) {
                auto letter = text_[position_++];
                if (letter == '"') {
                    closed = true;
                    break;
                }
                if (letter == '\\' && position_ < text_.size())
                    letter = text_[position_++];
                expression.atom += letter;
            }
            if (!closed)
                throw std::runtime_error("Unclosed KiCad string");
        } else {
            while (position_ < text_.size() && !text_[position_].isSpace() &&
                   text_[position_] != '(' && text_[position_] != ')')
                expression.atom += text_[position_++];
            if (expression.atom.isEmpty())
                throw std::runtime_error("Invalid KiCad token");
        }
        return expression;
    }

  private:
    void whitespace() {
        while (position_ < text_.size() && text_[position_].isSpace())
            ++position_;
    }

    QString text_;
    qsizetype position_{};
};

} // namespace levelcad::app::sexpr
