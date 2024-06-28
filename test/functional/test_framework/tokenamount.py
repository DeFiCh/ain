import re
from decimal import Decimal, InvalidOperation


class TokenAmount:
    def __init__(
        self, number_with_symbol: str = None, number: Decimal = None, symbol: str = None
    ):
        if number_with_symbol:
            if not self._validate(number_with_symbol):
                raise ValueError(
                    "String must be in the format NumberWith8DigitPrecision@SYMBOL"
                )
            self.number_with_symbol = number_with_symbol
        elif number is not None and symbol:
            try:
                number = Decimal(number)
            except InvalidOperation:
                raise ValueError("Number must be convertible to Decimal")
            if not self._validate_number(number):
                raise ValueError(
                    "Number must have up to 8 digit precision after the decimal point"
                )
            self.number_with_symbol = f"{number:.8f}@{symbol}"
        else:
            raise ValueError(
                "Invalid arguments. Provide either a single string or number and symbol."
            )

    def _validate(self, number_with_symbol: str) -> bool:
        pattern = r"^\d{1,32}(\.\d{1,8})?@\w+$"
        return bool(re.match(pattern, number_with_symbol))

    def _validate_number(self, number: Decimal) -> bool:
        return abs(number.as_tuple().exponent) <= 8

    def __str__(self):
        return self.number_with_symbol

    def __repr__(self):
        return f"TokenAmount({self.number_with_symbol!r})"

    def get_number(self) -> Decimal:
        number_str = self.number_with_symbol.split("@")[0]
        return Decimal(number_str)

    def get_symbol(self) -> str:
        return self.number_with_symbol.split("@")[1]

    def _new_with_number(self, new_number: Decimal):
        return TokenAmount(f"{new_number:.8f}@{self.get_symbol()}")

    def __add__(self, other):
        if isinstance(other, TokenAmount):
            if self.get_symbol() != other.get_symbol():
                raise ValueError("Cannot add: Symbols are different")
            new_number = self.get_number() + other.get_number()
        elif isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() + Decimal(other)
        else:
            return NotImplemented
        return self._new_with_number(new_number)

    def __sub__(self, other):
        if isinstance(other, TokenAmount):
            if self.get_symbol() != other.get_symbol():
                raise ValueError("Cannot subtract: Symbols are different")
            new_number = self.get_number() - other.get_number()
        elif isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() - Decimal(other)
        else:
            return NotImplemented
        return self._new_with_number(new_number)

    def __mul__(self, other):
        if isinstance(other, TokenAmount):
            if self.get_symbol() != other.get_symbol():
                raise ValueError("Cannot multiply: Symbols are different")
            new_number = self.get_number() * other.get_number()
        elif isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() * Decimal(other)
        else:
            return NotImplemented
        return self._new_with_number(new_number)

    def __truediv__(self, other):
        if isinstance(other, TokenAmount):
            if self.get_symbol() != other.get_symbol():
                raise ValueError("Cannot divide: Symbols are different")
            new_number = self.get_number() / other.get_number()
        elif isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() / Decimal(other)
        else:
            return NotImplemented
        return self._new_with_number(new_number)

    def __eq__(self, other):
        if isinstance(other, TokenAmount):
            return self.number_with_symbol == other.number_with_symbol
        return False
