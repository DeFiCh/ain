import re
from decimal import Decimal


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
            if not self._validate_number(number):
                raise ValueError("Number must have 8 digit precision")
            self.number_with_symbol = f"{number:.8f}@{symbol}"
        else:
            raise ValueError(
                "Invalid arguments. Provide either a single string or number and symbol."
            )

    def _validate(self, number_with_symbol: str) -> bool:
        pattern = r"^\d{1,32}\.\d{8}@\w+$"
        return bool(re.match(pattern, number_with_symbol))

    def __str__(self):
        return self.number_with_symbol

    def get_number(self) -> Decimal:
        number_str = self.number_with_symbol.split("@")[0]
        return Decimal(number_str)

    def get_symbol(self) -> str:
        return self.number_with_symbol.split("@")[1]

    def _update_number(self, new_number: Decimal):
        self.number_with_symbol = f"{new_number:.8f}@{self.get_symbol()}"

    def __add__(self, other):
        if isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() + Decimal(other)
            self._update_number(new_number)
        return self

    def __sub__(self, other):
        if isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() - Decimal(other)
            self._update_number(new_number)
        return self

    def __mul__(self, other):
        if isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() * Decimal(other)
            self._update_number(new_number)
        return self

    def __truediv__(self, other):
        if isinstance(other, (Decimal, int, float)):
            new_number = self.get_number() / Decimal(other)
            self._update_number(new_number)
        return self
